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
#ifndef SCORE_MEMORY_SHARED_INTERVM_SHARED_MEMORY_REGION_H
#define SCORE_MEMORY_SHARED_INTERVM_SHARED_MEMORY_REGION_H

#include "score/memory/shared/intervm/shared_memory_region_directory.h"
#include "score/memory/shared/typedshm/typedshm_wrapper/typed_memory.h"

#include "score/os/errno.h"

#include <score/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace score::memory::shared
{

/// \brief Provider for inter-VM shared memory, which places all named shm-objects into a single shared memory region.
/// \details The region is provided by the hypervisor (e.g. a region of the QNX hypervisor's shmem device) and is
///          accessible on every VM via a file: On the hypervisor host it is a POSIX shm-object (e.g.
///          /dev/shmem/QVMSHM-<name> on QNX), within a guest it is the device node of the driver of the shared memory
///          device. The file has to support mmap() with offsets. The region may start at an offset within the file,
///          e.g. behind the control page of the shared memory device. It starts with a SharedMemoryRegionDirectory,
///          which maps the names of the shm-objects to their location in the region. Every process, which creates or
///          opens inter-VM shm-objects, uses its own instance on the same region. The region is opened lazily, when the
///          first shm-object is created or opened, so that a missing region is reported as an error of that call.
class SharedMemoryRegion final : public TypedMemory
{
  public:
    /// \param region_path path of the file, which gives access to the region.
    /// \param region_offset offset of the region within the file in bytes. It has to be a multiple of the page size.
    /// \param region_size size of the region in bytes. It has to be the same on all VMs.
    SharedMemoryRegion(std::string region_path,
                       const std::uint64_t region_offset,
                       const std::uint64_t region_size) noexcept;

    ~SharedMemoryRegion() noexcept override;

    SharedMemoryRegion(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion(SharedMemoryRegion&&) = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion& operator=(SharedMemoryRegion&&) = delete;

    /// \brief Not supported: shm-objects are created within the region via CreateNamedRegionObject().
    score::cpp::expected_blank<score::os::Error> AllocateNamedTypedMemory(
        const std::size_t shm_size,
        const std::string shm_name,
        const permission::UserPermissions& permissions) const noexcept override;

    /// \brief Not supported: anonymous shm-objects can't be shared between VMs.
    score::cpp::expected<int, score::os::Error> AllocateAndOpenAnonymousTypedMemory(
        const std::uint64_t shm_size) const noexcept override;

    /// \brief Does nothing, since shm-objects within the region are never freed. Creating a shm-object again reuses
    ///        it.
    score::cpp::expected_blank<score::os::Error> Unlink(std::string_view shm_name) const noexcept override;

    /// \brief Not supported: the region isn't managed by the typed memory daemon.
    score::cpp::expected<uid_t, score::os::Error> GetCreatorUid(std::string_view shm_name) const noexcept override;

    score::cpp::expected<RegionObject, score::os::Error> CreateNamedRegionObject(
        std::string_view shm_name,
        const std::size_t shm_size) const noexcept override;

    score::cpp::expected<RegionObject, score::os::Error> OpenNamedRegionObject(
        std::string_view shm_name) const noexcept override;

  private:
    /// \brief Opens the region and attaches to its directory, unless this has been done already.
    score::cpp::expected<SharedMemoryRegionDirectory*, score::os::Error> AttachToRegion() const noexcept;

    /// \brief Hands out the given shm-object with its own file descriptor of the region.
    score::cpp::expected<RegionObject, score::os::Error> MakeRegionObject(
        const SharedMemoryRegionDirectory::Object& object) const noexcept;

    const std::string region_path_;
    const std::uint64_t region_offset_;
    const std::uint64_t region_size_;

    mutable std::mutex mutex_;
    mutable std::int32_t region_file_descriptor_;
    mutable void* directory_address_;
    mutable std::optional<SharedMemoryRegionDirectory> directory_;
};

}  // namespace score::memory::shared

#endif  // SCORE_MEMORY_SHARED_INTERVM_SHARED_MEMORY_REGION_H
