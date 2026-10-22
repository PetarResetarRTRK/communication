/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
#include "score/memory/shared/intervm/shared_memory_region_directory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <type_traits>

namespace score::memory::shared
{

namespace
{

constexpr std::uint32_t kUninitialized{0U};
constexpr std::uint32_t kInitializing{1U};
constexpr std::uint32_t kReady{2U};

constexpr std::uint32_t kUnlocked{0U};
constexpr std::uint32_t kLocked{1U};

/// \brief "LDIR" - marks the start of a region, which contains a directory.
constexpr std::uint32_t kMagic{0x5249444CU};
constexpr std::uint32_t kVersion{1U};

/// \brief The directory is only locked for a few lookups or a single update, so waiting longer than this means, that
///        the user holding the lock (or initializing the directory) died.
constexpr std::chrono::milliseconds kTimeout{1000};

// The directory is accessed from different processes and VMs, so the atomics within it must not rely on any OS support
// and must have the same layout everywhere.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "Directory requires lock free atomics");
static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t), "Unexpected size of atomics");

struct Entry
{
    std::array<char, SharedMemoryRegionDirectory::kMaxNameLength> name;
    std::uint64_t offset;
    std::uint64_t size;
    /// \brief Size reserved for the shm-object within the region, i.e. its size aligned to kObjectAlignment.
    std::uint64_t capacity;
};

struct Header
{
    std::atomic<std::uint32_t> state;
    std::uint32_t magic;
    std::uint32_t version;
    std::atomic<std::uint32_t> lock;
    std::uint64_t region_size;
    std::uint64_t next_free_offset;
    std::uint32_t number_of_objects;
    std::uint32_t reserved;
};

static_assert(sizeof(Entry) == 152U, "Unexpected layout of directory entries");
static_assert(sizeof(Header) == 40U, "Unexpected layout of directory header");

bool AlignToObjectAlignment(const std::uint64_t value, std::uint64_t& aligned_value) noexcept
{
    constexpr auto alignment = SharedMemoryRegionDirectory::kObjectAlignment;
    if (value > (std::numeric_limits<std::uint64_t>::max() - (alignment - 1U)))
    {
        return false;
    }
    aligned_value = ((value + (alignment - 1U)) / alignment) * alignment;
    return true;
}

score::cpp::unexpected<score::os::Error> MakeError(const std::int32_t error_number) noexcept
{
    return score::cpp::make_unexpected(score::os::Error::createFromErrno(error_number));
}

score::cpp::expected_blank<score::os::Error> CheckName(std::string_view name) noexcept
{
    if (name.empty())
    {
        return MakeError(EINVAL);
    }
    if (name.size() >= SharedMemoryRegionDirectory::kMaxNameLength)
    {
        return MakeError(ENAMETOOLONG);
    }
    return {};
}

/// \brief Locks the directory for the lifetime of this object, if locked() returns true.
class DirectoryLock final
{
  public:
    explicit DirectoryLock(std::atomic<std::uint32_t>& lock) noexcept : lock_{lock}, locked_{false}
    {
        const auto deadline = std::chrono::steady_clock::now() + kTimeout;
        while (true)
        {
            auto expected = kUnlocked;
            if (lock_.compare_exchange_weak(expected, kLocked, std::memory_order_acquire, std::memory_order_relaxed))
            {
                locked_ = true;
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return;
            }
            std::this_thread::yield();
        }
    }

    ~DirectoryLock() noexcept
    {
        if (locked_)
        {
            lock_.store(kUnlocked, std::memory_order_release);
        }
    }

    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock(DirectoryLock&&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
    DirectoryLock& operator=(DirectoryLock&&) = delete;

    bool locked() const noexcept
    {
        return locked_;
    }

  private:
    std::atomic<std::uint32_t>& lock_;
    bool locked_;
};

}  // namespace

struct SharedMemoryRegionDirectory::Layout
{
    Header header;
    std::array<Entry, kMaxNumberOfObjects> entries;
};

static_assert(std::is_standard_layout_v<SharedMemoryRegionDirectory::Layout>, "Directory has to be standard layout");

namespace
{

Entry* FindEntry(SharedMemoryRegionDirectory::Layout& layout, std::string_view name) noexcept
{
    // The directory is shared with other VMs, so it isn't trusted to be consistent: The number of objects and the
    // names are bounded by the size of the directory.
    const auto number_of_objects = std::min(static_cast<std::size_t>(layout.header.number_of_objects),
                                            SharedMemoryRegionDirectory::kMaxNumberOfObjects);
    for (std::size_t index = 0U; index < number_of_objects; ++index)
    {
        auto& entry = layout.entries[index];
        const std::string_view entry_name{entry.name.data(), ::strnlen(entry.name.data(), entry.name.size())};
        if (entry_name == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

std::uint64_t SharedMemoryRegionDirectory::GetSize() noexcept
{
    std::uint64_t size{};
    static_cast<void>(AlignToObjectAlignment(sizeof(Layout), size));
    return size;
}

score::cpp::expected<SharedMemoryRegionDirectory, score::os::Error> SharedMemoryRegionDirectory::Attach(
    void* const directory_address,
    const std::uint64_t region_size) noexcept
{
    if ((directory_address == nullptr) || (region_size < GetSize()))
    {
        return MakeError(EINVAL);
    }

    // The region is zero-initialized when it is created by the hypervisor (or the OS), so the directory is
    // uninitialized until its first user initializes it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): The directory lives in shared memory.
    auto& layout = *static_cast<Layout*>(directory_address);
    auto& header = layout.header;

    auto expected_state = kUninitialized;
    if (header.state.compare_exchange_strong(
            expected_state, kInitializing, std::memory_order_acquire, std::memory_order_acquire))
    {
        header.magic = kMagic;
        header.version = kVersion;
        header.lock.store(kUnlocked, std::memory_order_relaxed);
        header.region_size = region_size;
        header.next_free_offset = GetSize();
        header.number_of_objects = 0U;
        header.reserved = 0U;
        header.state.store(kReady, std::memory_order_release);
    }
    else
    {
        const auto deadline = std::chrono::steady_clock::now() + kTimeout;
        while (header.state.load(std::memory_order_acquire) == kInitializing)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return MakeError(ETIMEDOUT);
            }
            std::this_thread::yield();
        }
    }

    if ((header.state.load(std::memory_order_acquire) != kReady) || (header.magic != kMagic) ||
        (header.version != kVersion) || (header.region_size != region_size))
    {
        return MakeError(EINVAL);
    }
    return SharedMemoryRegionDirectory{layout};
}

score::cpp::expected<SharedMemoryRegionDirectory::Object, score::os::Error> SharedMemoryRegionDirectory::Create(
    std::string_view name,
    const std::uint64_t size) noexcept
{
    const auto name_check = CheckName(name);
    if (!name_check.has_value())
    {
        return score::cpp::make_unexpected(name_check.error());
    }
    std::uint64_t capacity{};
    if ((size == 0U) || (!AlignToObjectAlignment(size, capacity)))
    {
        return MakeError(EINVAL);
    }

    auto& header = layout_->header;
    const DirectoryLock lock{header.lock};
    if (!lock.locked())
    {
        return MakeError(ETIMEDOUT);
    }

    auto* const existing_entry = FindEntry(*layout_, name);
    if (existing_entry != nullptr)
    {
        if (existing_entry->capacity < size)
        {
            return MakeError(ENOSPC);
        }
        existing_entry->size = size;
        return Object{existing_entry->offset, existing_entry->size};
    }

    if (header.number_of_objects >= kMaxNumberOfObjects)
    {
        return MakeError(ENOSPC);
    }
    const auto offset = header.next_free_offset;
    if ((offset > header.region_size) || (capacity > (header.region_size - offset)))
    {
        return MakeError(ENOMEM);
    }

    auto& entry = layout_->entries[header.number_of_objects];
    entry.name.fill('\0');
    // The name has been checked to be shorter than the entry, so it stays null terminated.
    static_cast<void>(std::memcpy(entry.name.data(), name.data(), name.size()));
    entry.offset = offset;
    entry.size = size;
    entry.capacity = capacity;
    header.next_free_offset = offset + capacity;
    header.number_of_objects = header.number_of_objects + 1U;
    return Object{entry.offset, entry.size};
}

score::cpp::expected<SharedMemoryRegionDirectory::Object, score::os::Error> SharedMemoryRegionDirectory::Open(
    std::string_view name) const noexcept
{
    const auto name_check = CheckName(name);
    if (!name_check.has_value())
    {
        return score::cpp::make_unexpected(name_check.error());
    }

    const DirectoryLock lock{layout_->header.lock};
    if (!lock.locked())
    {
        return MakeError(ETIMEDOUT);
    }

    const auto* const entry = FindEntry(*layout_, name);
    if (entry == nullptr)
    {
        return MakeError(ENOENT);
    }
    return Object{entry->offset, entry->size};
}

}  // namespace score::memory::shared
