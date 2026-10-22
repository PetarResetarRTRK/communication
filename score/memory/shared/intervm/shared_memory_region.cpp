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
#include "score/memory/shared/intervm/shared_memory_region.h"

#include "score/mw/log/logging.h"
#include "score/os/errno_logging.h"
#include "score/os/fcntl.h"
#include "score/os/mman.h"
#include "score/os/stat.h"
#include "score/os/unistd.h"

#include <score/utility.hpp>

#include <cerrno>
#include <limits>
#include <utility>

namespace score::memory::shared
{

namespace
{

score::cpp::unexpected<score::os::Error> MakeError(const std::int32_t error_number) noexcept
{
    return score::cpp::make_unexpected(score::os::Error::createFromErrno(error_number));
}

}  // namespace

SharedMemoryRegion::SharedMemoryRegion(std::string region_path, const std::uint64_t region_size) noexcept
    : TypedMemory{},
      region_path_{std::move(region_path)},
      region_size_{region_size},
      mutex_{},
      region_file_descriptor_{-1},
      directory_address_{nullptr},
      directory_{}
{
}

SharedMemoryRegion::~SharedMemoryRegion() noexcept
{
    if (directory_address_ != nullptr)
    {
        score::cpp::ignore =
            score::os::Mman::instance().munmap(directory_address_, SharedMemoryRegionDirectory::GetSize());
    }
    if (region_file_descriptor_ != -1)
    {
        score::cpp::ignore = score::os::Unistd::instance().close(region_file_descriptor_);
    }
}

score::cpp::expected_blank<score::os::Error> SharedMemoryRegion::AllocateNamedTypedMemory(
    const std::size_t /*shm_size*/,
    const std::string /*shm_name*/,
    const permission::UserPermissions& /*permissions*/) const noexcept
{
    return MakeError(ENOTSUP);
}

score::cpp::expected<int, score::os::Error> SharedMemoryRegion::AllocateAndOpenAnonymousTypedMemory(
    const std::uint64_t /*shm_size*/) const noexcept
{
    return MakeError(ENOTSUP);
}

score::cpp::expected_blank<score::os::Error> SharedMemoryRegion::Unlink(std::string_view /*shm_name*/) const noexcept
{
    return {};
}

score::cpp::expected<uid_t, score::os::Error> SharedMemoryRegion::GetCreatorUid(
    std::string_view /*shm_name*/) const noexcept
{
    return MakeError(ENOTSUP);
}

score::cpp::expected<TypedMemory::RegionObject, score::os::Error> SharedMemoryRegion::CreateNamedRegionObject(
    std::string_view shm_name,
    const std::size_t shm_size) const noexcept
{
    const auto directory = AttachToRegion();
    if (!directory.has_value())
    {
        return score::cpp::make_unexpected(directory.error());
    }
    const auto object = directory.value()->Create(shm_name, shm_size);
    if (!object.has_value())
    {
        score::mw::log::LogError("shm") << "Could not create" << shm_name << "with size" << shm_size
                                        << "in shared memory region" << region_path_ << ":" << object.error();
        return score::cpp::make_unexpected(object.error());
    }
    return MakeRegionObject(object.value());
}

score::cpp::expected<TypedMemory::RegionObject, score::os::Error> SharedMemoryRegion::OpenNamedRegionObject(
    std::string_view shm_name) const noexcept
{
    const auto directory = AttachToRegion();
    if (!directory.has_value())
    {
        return score::cpp::make_unexpected(directory.error());
    }
    const auto object = directory.value()->Open(shm_name);
    if (!object.has_value())
    {
        return score::cpp::make_unexpected(object.error());
    }
    return MakeRegionObject(object.value());
}

score::cpp::expected<SharedMemoryRegionDirectory*, score::os::Error> SharedMemoryRegion::AttachToRegion() const noexcept
{
    std::lock_guard<std::mutex> lock{mutex_};
    if (directory_.has_value())
    {
        return &directory_.value();
    }

    const auto directory_size = SharedMemoryRegionDirectory::GetSize();
    if (region_size_ < directory_size)
    {
        score::mw::log::LogError("shm") << "Shared memory region" << region_path_ << "with size" << region_size_
                                        << "is too small for its directory of size" << directory_size;
        return MakeError(EINVAL);
    }

    const auto open_result = score::os::Fcntl::instance().open(
        region_path_.c_str(), score::os::Fcntl::Open::kReadWrite | score::os::Fcntl::Open::kCloseOnExec);
    if (!open_result.has_value())
    {
        score::mw::log::LogError("shm") << "Could not open shared memory region" << region_path_ << ":"
                                        << open_result.error();
        return score::cpp::make_unexpected(open_result.error());
    }
    const auto file_descriptor = open_result.value();

    // POSIX shm-objects report their size, device nodes report 0.
    score::os::StatBuffer stat_buffer{};
    const auto stat_result = score::os::Stat::instance().fstat(file_descriptor, stat_buffer);
    const bool is_too_small = stat_result.has_value() && (stat_buffer.st_size > 0) &&
                              (static_cast<std::uint64_t>(stat_buffer.st_size) < region_size_);
    if ((!stat_result.has_value()) || is_too_small)
    {
        score::mw::log::LogError("shm") << "Shared memory region" << region_path_
                                        << "is smaller than its configured size" << region_size_
                                        << "or can't be queried";
        score::cpp::ignore = score::os::Unistd::instance().close(file_descriptor);
        return stat_result.has_value() ? MakeError(EINVAL) : score::cpp::make_unexpected(stat_result.error());
    }

    const auto mmap_result =
        score::os::Mman::instance().mmap(nullptr,
                                         directory_size,
                                         score::os::Mman::Protection::kRead | score::os::Mman::Protection::kWrite,
                                         score::os::Mman::Map::kShared,
                                         file_descriptor,
                                         0);
    if (!mmap_result.has_value())
    {
        score::mw::log::LogError("shm") << "Could not map the directory of shared memory region" << region_path_ << ":"
                                        << mmap_result.error();
        score::cpp::ignore = score::os::Unistd::instance().close(file_descriptor);
        return score::cpp::make_unexpected(mmap_result.error());
    }

    auto directory = SharedMemoryRegionDirectory::Attach(mmap_result.value(), region_size_);
    if (!directory.has_value())
    {
        score::mw::log::LogError("shm") << "Shared memory region" << region_path_
                                        << "doesn't contain a valid directory for size" << region_size_ << ":"
                                        << directory.error();
        score::cpp::ignore = score::os::Mman::instance().munmap(mmap_result.value(), directory_size);
        score::cpp::ignore = score::os::Unistd::instance().close(file_descriptor);
        return score::cpp::make_unexpected(directory.error());
    }

    region_file_descriptor_ = file_descriptor;
    directory_address_ = mmap_result.value();
    directory_.emplace(directory.value());
    return &directory_.value();
}

score::cpp::expected<TypedMemory::RegionObject, score::os::Error> SharedMemoryRegion::MakeRegionObject(
    const SharedMemoryRegionDirectory::Object& object) const noexcept
{
    if (object.size > std::numeric_limits<std::size_t>::max())
    {
        return MakeError(EINVAL);
    }
    // The shm-object gets its own file descriptor, since its user closes it, when it doesn't need it anymore.
    const auto dup_result = score::os::Unistd::instance().dup(region_file_descriptor_);
    if (!dup_result.has_value())
    {
        score::mw::log::LogError("shm") << "Could not duplicate the file descriptor of shared memory region"
                                        << region_path_ << ":" << dup_result.error();
        return score::cpp::make_unexpected(dup_result.error());
    }
    return RegionObject{dup_result.value(), object.offset, static_cast<std::size_t>(object.size)};
}

}  // namespace score::memory::shared
