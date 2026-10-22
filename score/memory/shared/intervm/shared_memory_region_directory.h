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
#ifndef SCORE_MEMORY_SHARED_INTERVM_SHARED_MEMORY_REGION_DIRECTORY_H
#define SCORE_MEMORY_SHARED_INTERVM_SHARED_MEMORY_REGION_DIRECTORY_H

#include "score/os/errno.h"

#include <score/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace score::memory::shared
{

/// \brief Directory of the named shm-objects within a shared memory region. It is located at the start of the region.
/// \details The region is shared between processes on different VMs, which might run different OSes. So the layout of
///          the directory only consists of fixed width types, and the directory is protected by a spin lock within the
///          region instead of an OS specific mutex. shm-objects are allocated one after the other behind the directory
///          and are never freed: Creating a shm-object, which already exists, reuses it.
class SharedMemoryRegionDirectory final
{
  public:
    /// \brief Maximum number of shm-objects within a region.
    static constexpr std::size_t kMaxNumberOfObjects{256U};

    /// \brief Maximum length of the name of a shm-object, including the terminating null character.
    static constexpr std::size_t kMaxNameLength{128U};

    /// \brief Alignment of the shm-objects within the region. It is a multiple of all page sizes in use (4, 16 and 64
    ///        KiB), so that each shm-object can be mapped on its own on each VM.
    static constexpr std::uint64_t kObjectAlignment{64U * 1024U};

    /// \brief Location of a shm-object within the region.
    struct Object
    {
        std::uint64_t offset;
        std::uint64_t size;
    };

    /// \brief Size of the directory at the start of the region. The first shm-object is located directly behind it.
    static std::uint64_t GetSize() noexcept;

    /// \brief Attaches to the directory at the given address, which is the start of the region. Initializes the
    ///        directory, if nobody did this yet.
    /// \param directory_address address of the region, to which at least GetSize() bytes are mapped.
    /// \param region_size size of the region in bytes. It has to be the same for all users of the region.
    /// \return the attached directory, or kInvalidArgument, if the region doesn't contain a valid directory for the
    ///         given region size, or ETIMEDOUT, if the directory didn't get initialized in time.
    static score::cpp::expected<SharedMemoryRegionDirectory, score::os::Error> Attach(
        void* const directory_address,
        const std::uint64_t region_size) noexcept;

    /// \brief Creates a shm-object with the given name and size, or reuses the existing shm-object with this name, if
    ///        it is large enough.
    /// \return the location of the shm-object, or kInvalidArgument for an empty name or zero size, or kFilenameTooLong,
    ///         or kNoSpaceLeftOnDevice, if there is no free entry or the existing shm-object is too small, or
    ///         kNotEnoughSpace, if the region is full, or ETIMEDOUT, if the directory couldn't be locked in time.
    score::cpp::expected<Object, score::os::Error> Create(std::string_view name, const std::uint64_t size) noexcept;

    /// \brief Looks up the shm-object with the given name.
    /// \return the location of the shm-object, or kNoSuchFileOrDirectory, if there is none, or kFilenameTooLong, or
    ///         ETIMEDOUT, if the directory couldn't be locked in time.
    score::cpp::expected<Object, score::os::Error> Open(std::string_view name) const noexcept;

    struct Layout;

  private:
    explicit SharedMemoryRegionDirectory(Layout& layout) noexcept : layout_{&layout} {}

    Layout* layout_;
};

}  // namespace score::memory::shared

#endif  // SCORE_MEMORY_SHARED_INTERVM_SHARED_MEMORY_REGION_DIRECTORY_H
