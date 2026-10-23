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

#include "score/memory/shared/shared_memory_factory.h"
#include "score/os/fcntl.h"
#include "score/os/mman.h"
#include "score/os/stat.h"
#include "score/os/unistd.h"

#include <score/utility.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace score::memory::shared
{
namespace
{

using Error = score::os::Error;

constexpr std::uint64_t kRegionSize{4U * 1024U * 1024U};
// Offset of the region within its file, e.g. behind a control page, with a size, which is a multiple of all page sizes.
constexpr std::uint64_t kRegionOffset{64U * 1024U};
constexpr std::uint8_t kControlPageContent{0xC3U};
constexpr auto kRegionShmName = "/score_memory_shared_intervm_region_test";
#if defined(__QNXNTO__)
constexpr auto kRegionPath = "/dev/shmem/score_memory_shared_intervm_region_test";
#else
constexpr auto kRegionPath = "/dev/shm/score_memory_shared_intervm_region_test";
#endif
constexpr auto kObjectName = "/intervm-shared-shmem/lola-data-0000000000004711-00001";
constexpr std::uint64_t kValue{0x0123456789ABCDEFU};

/// \brief Maps the given shm-object of the region and closes its file descriptor, when it goes out of scope.
class MappedRegionObject final
{
  public:
    explicit MappedRegionObject(const TypedMemory::RegionObject& region_object)
        : region_object_{region_object}, address_{nullptr}
    {
        const auto result =
            score::os::Mman::instance().mmap(nullptr,
                                             region_object_.size,
                                             score::os::Mman::Protection::kRead | score::os::Mman::Protection::kWrite,
                                             score::os::Mman::Map::kShared,
                                             region_object_.file_descriptor,
                                             static_cast<std::int64_t>(region_object_.offset));
        EXPECT_TRUE(result.has_value());
        if (result.has_value())
        {
            address_ = static_cast<std::uint8_t*>(result.value());
        }
    }

    ~MappedRegionObject()
    {
        if (address_ != nullptr)
        {
            score::cpp::ignore = score::os::Mman::instance().munmap(address_, region_object_.size);
        }
        score::cpp::ignore = score::os::Unistd::instance().close(region_object_.file_descriptor);
    }

    MappedRegionObject(const MappedRegionObject&) = delete;
    MappedRegionObject(MappedRegionObject&&) = delete;
    MappedRegionObject& operator=(const MappedRegionObject&) = delete;
    MappedRegionObject& operator=(MappedRegionObject&&) = delete;

    std::uint8_t* Address() const
    {
        return address_;
    }

  private:
    TypedMemory::RegionObject region_object_;
    std::uint8_t* address_;
};

class SharedMemoryRegionTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // The region of the hypervisor is simulated by a POSIX shm-object.
        CreateRegion(kRegionSize);
    }

    void TearDown() override
    {
        score::cpp::ignore = score::os::Mman::instance().shm_unlink(kRegionShmName);
    }

    static void CreateRegion(const std::uint64_t size)
    {
        score::cpp::ignore = score::os::Mman::instance().shm_unlink(kRegionShmName);
        const auto file_descriptor = score::os::Mman::instance().shm_open(
            kRegionShmName,
            score::os::Fcntl::Open::kReadWrite | score::os::Fcntl::Open::kCreate | score::os::Fcntl::Open::kExclusive,
            score::os::Stat::Mode::kReadUser | score::os::Stat::Mode::kWriteUser);
        ASSERT_TRUE(file_descriptor.has_value());
        ASSERT_TRUE(
            score::os::Unistd::instance().ftruncate(file_descriptor.value(), static_cast<off_t>(size)).has_value());
        score::cpp::ignore = score::os::Unistd::instance().close(file_descriptor.value());
    }

    /// \brief Maps the part of the file in front of a region at kRegionOffset (e.g. a control page) for the given
    /// access.
    template <typename Access>
    static void AccessControlPage(Access access)
    {
        const auto file_descriptor =
            score::os::Mman::instance().shm_open(kRegionShmName,
                                                 score::os::Fcntl::Open::kReadWrite,
                                                 score::os::Stat::Mode::kReadUser | score::os::Stat::Mode::kWriteUser);
        ASSERT_TRUE(file_descriptor.has_value());
        const auto address =
            score::os::Mman::instance().mmap(nullptr,
                                             kRegionOffset,
                                             score::os::Mman::Protection::kRead | score::os::Mman::Protection::kWrite,
                                             score::os::Mman::Map::kShared,
                                             file_descriptor.value(),
                                             0);
        ASSERT_TRUE(address.has_value());
        access(static_cast<std::uint8_t*>(address.value()));
        score::cpp::ignore = score::os::Mman::instance().munmap(address.value(), kRegionOffset);
        score::cpp::ignore = score::os::Unistd::instance().close(file_descriptor.value());
    }
};

TEST_F(SharedMemoryRegionTest, ObjectCreatedByOneUserOfTheRegionCanBeOpenedByAnother)
{
    // Given two users of the same region, e.g. on different VMs
    const SharedMemoryRegion creator{kRegionPath, 0U, kRegionSize};
    const SharedMemoryRegion opener{kRegionPath, 0U, kRegionSize};

    // When one of them creates an object and writes to it
    const auto created = creator.CreateNamedRegionObject(kObjectName, 4096U);
    ASSERT_TRUE(created.has_value());
    const MappedRegionObject created_mapping{created.value()};
    ASSERT_NE(created_mapping.Address(), nullptr);
    std::memset(created_mapping.Address(), 0x5A, 4096U);

    // Then the other one can open it
    const auto opened = opener.OpenNamedRegionObject(kObjectName);
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value().offset, created.value().offset);
    EXPECT_EQ(opened.value().size, 4096U);
    EXPECT_NE(opened.value().file_descriptor, created.value().file_descriptor);

    // and sees the written data
    const MappedRegionObject opened_mapping{opened.value()};
    ASSERT_NE(opened_mapping.Address(), nullptr);
    EXPECT_EQ(opened_mapping.Address()[0], 0x5A);
    EXPECT_EQ(opened_mapping.Address()[4095], 0x5A);
}

TEST_F(SharedMemoryRegionTest, ObjectIsPlacedBehindTheDirectoryOfTheRegion)
{
    const SharedMemoryRegion region{kRegionPath, 0U, kRegionSize};

    // When creating an object
    const auto created = region.CreateNamedRegionObject(kObjectName, 100U);

    // Then it is located behind the directory at the start of the region
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created.value().offset, SharedMemoryRegionDirectory::GetSize());
    EXPECT_EQ(created.value().size, 100U);
    score::cpp::ignore = score::os::Unistd::instance().close(created.value().file_descriptor);
}

TEST_F(SharedMemoryRegionTest, OpeningAnUnknownObjectFails)
{
    const SharedMemoryRegion region{kRegionPath, 0U, kRegionSize};

    // When opening an object, which hasn't been created
    const auto result = region.OpenNamedRegionObject(kObjectName);

    // Then it fails
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kNoSuchFileOrDirectory);
}

TEST_F(SharedMemoryRegionTest, AccessFailsIfTheRegionDoesNotExist)
{
    // Given a region, which doesn't exist (yet)
    TearDown();
    const SharedMemoryRegion region{kRegionPath, 0U, kRegionSize};

    // When creating an object
    // Then it fails
    EXPECT_FALSE(region.CreateNamedRegionObject(kObjectName, 100U).has_value());

    // and succeeds as soon as the region exists
    CreateRegion(kRegionSize);
    const auto result = region.CreateNamedRegionObject(kObjectName, 100U);
    ASSERT_TRUE(result.has_value());
    score::cpp::ignore = score::os::Unistd::instance().close(result.value().file_descriptor);
}

TEST_F(SharedMemoryRegionTest, AccessFailsIfTheRegionIsSmallerThanConfigured)
{
    // Given a region, which is smaller than configured
    const SharedMemoryRegion region{kRegionPath, 0U, kRegionSize * 2U};

    // When creating an object
    const auto result = region.CreateNamedRegionObject(kObjectName, 100U);

    // Then it fails
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kInvalidArgument);
}

TEST_F(SharedMemoryRegionTest, RegionStartsAtItsOffsetWithinTheFile)
{
    // Given a file, in which the region starts behind a control page, which must not be touched
    CreateRegion(kRegionOffset + kRegionSize);
    AccessControlPage([](std::uint8_t* const control_page) {
        std::memset(control_page, kControlPageContent, kRegionOffset);
    });
    const SharedMemoryRegion creator{kRegionPath, kRegionOffset, kRegionSize};
    const SharedMemoryRegion opener{kRegionPath, kRegionOffset, kRegionSize};

    // When creating an object
    const auto created = creator.CreateNamedRegionObject(kObjectName, 100U);

    // Then it is located behind the directory at the start of the region
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created.value().offset, kRegionOffset + SharedMemoryRegionDirectory::GetSize());
    score::cpp::ignore = score::os::Unistd::instance().close(created.value().file_descriptor);

    // and another user of the region finds it there
    const auto opened = opener.OpenNamedRegionObject(kObjectName);
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value().offset, created.value().offset);
    score::cpp::ignore = score::os::Unistd::instance().close(opened.value().file_descriptor);

    // and the control page is unchanged
    AccessControlPage([](const std::uint8_t* const control_page) {
        EXPECT_TRUE(std::all_of(control_page, control_page + kRegionOffset, [](const std::uint8_t byte) {
            return byte == kControlPageContent;
        }));
    });
}

TEST_F(SharedMemoryRegionTest, AccessFailsIfTheRegionDoesNotFitBehindItsOffset)
{
    // Given a file, which has the size of the region, but in which the region starts at an offset
    const SharedMemoryRegion region{kRegionPath, kRegionOffset, kRegionSize};

    // When creating an object
    const auto result = region.CreateNamedRegionObject(kObjectName, 100U);

    // Then it fails
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kInvalidArgument);
}

TEST_F(SharedMemoryRegionTest, TypedMemoryOperationsAreNotSupported)
{
    const SharedMemoryRegion region{kRegionPath, 0U, kRegionSize};

    // When using the typed memory operations
    // Then they are not supported, but unlinking does nothing and succeeds
    EXPECT_EQ(region.AllocateNamedTypedMemory(100U, kObjectName, permission::WorldWritable{}).error(),
              Error::Code::kOperationNotSupported);
    EXPECT_EQ(region.AllocateAndOpenAnonymousTypedMemory(100U).error(), Error::Code::kOperationNotSupported);
    EXPECT_EQ(region.GetCreatorUid(kObjectName).error(), Error::Code::kOperationNotSupported);
    EXPECT_TRUE(region.Unlink(kObjectName).has_value());
}

TEST_F(SharedMemoryRegionTest, SharedMemoryFactoryPlacesInterVmShmObjectsIntoTheRegion)
{
    // Given the region being used as inter-VM memory provider of this process
    SharedMemoryFactory::SetInterVMMemoryProvider(std::make_shared<SharedMemoryRegion>(kRegionPath, 0U, kRegionSize));

    // When creating an inter-VM shm-object and writing to it
    const auto resource = SharedMemoryFactory::Create(
        kObjectName,
        [](std::shared_ptr<ISharedMemoryResource> memory_resource) {
            score::cpp::ignore = memory_resource->construct<std::uint64_t>(kValue);
        },
        1024U,
        SharedMemoryFactory::WorldWritable{});
    ASSERT_NE(resource, nullptr);

    // Then another user of the region (e.g. on another VM) sees the shm-object with the same content
    const SharedMemoryRegion other_user{kRegionPath, 0U, kRegionSize};
    const auto opened = other_user.OpenNamedRegionObject(kObjectName);
    ASSERT_TRUE(opened.has_value());
    const MappedRegionObject opened_mapping{opened.value()};
    ASSERT_NE(opened_mapping.Address(), nullptr);
    const auto* const resource_base = static_cast<const std::uint8_t*>(resource->getBaseAddress());
    const auto used_bytes =
        static_cast<std::size_t>(static_cast<const std::uint8_t*>(resource->getUsableBaseAddress()) - resource_base) +
        sizeof(std::uint64_t);
    EXPECT_EQ(std::memcmp(opened_mapping.Address(), resource_base, used_bytes), 0);

    SharedMemoryFactory::Remove(kObjectName);
    SharedMemoryFactory::SetInterVMMemoryProvider(nullptr);
}

TEST_F(SharedMemoryRegionTest, SharedMemoryFactoryDoesNotFallBackToLocalShmIfTheRegionIsFull)
{
    // Given the region being used as inter-VM memory provider of this process
    SharedMemoryFactory::SetInterVMMemoryProvider(std::make_shared<SharedMemoryRegion>(kRegionPath, 0U, kRegionSize));

    // When creating an inter-VM shm-object, which doesn't fit into the region
    const auto resource = SharedMemoryFactory::Create(
        kObjectName, [](std::shared_ptr<ISharedMemoryResource>) {}, kRegionSize, SharedMemoryFactory::WorldWritable{});

    // Then it fails, instead of creating a local shm-object, which wouldn't be visible on other VMs
    EXPECT_EQ(resource, nullptr);
    const SharedMemoryRegion other_user{kRegionPath, 0U, kRegionSize};
    EXPECT_FALSE(other_user.OpenNamedRegionObject(kObjectName).has_value());

    SharedMemoryFactory::SetInterVMMemoryProvider(nullptr);
}

TEST_F(SharedMemoryRegionTest, SharedMemoryFactoryOpensInterVmShmObjectsFromTheRegion)
{
    // Given an inter-VM shm-object, which has been created by another user of the region (e.g. on another VM)
    SharedMemoryFactory::SetInterVMMemoryProvider(std::make_shared<SharedMemoryRegion>(kRegionPath, 0U, kRegionSize));
    {
        const auto resource = SharedMemoryFactory::Create(
            kObjectName,
            [](std::shared_ptr<ISharedMemoryResource> memory_resource) {
                score::cpp::ignore = memory_resource->construct<std::uint64_t>(kValue);
            },
            1024U,
            SharedMemoryFactory::WorldWritable{});
        ASSERT_NE(resource, nullptr);
    }

    // When opening it
    const auto resource = SharedMemoryFactory::Open(kObjectName, false);

    // Then its content is available
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(*static_cast<const std::uint64_t*>(resource->getUsableBaseAddress()), kValue);
    EXPECT_FALSE(resource->IsShmInTypedMemory());

    SharedMemoryFactory::SetInterVMMemoryProvider(nullptr);
}

}  // namespace
}  // namespace score::memory::shared
