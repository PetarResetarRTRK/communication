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

#include <score/utility.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace score::memory::shared
{
namespace
{

using Error = score::os::Error;

constexpr std::uint64_t kRegionSize{16U * 1024U * 1024U};

// Offsets of header fields, which the tests manipulate to simulate a corrupted directory or another user.
constexpr std::size_t kMagicOffset{4U};
constexpr std::size_t kLockOffset{12U};
constexpr std::size_t kNumberOfObjectsOffset{32U};

class SharedMemoryRegionDirectoryTest : public ::testing::Test
{
  protected:
    // Only the directory part of the region is ever accessed by the directory, so only this part is allocated.
    // std::uint64_t elements make the memory sufficiently aligned for the directory.
    std::vector<std::uint64_t> memory_ =
        std::vector<std::uint64_t>(SharedMemoryRegionDirectory::GetSize() / sizeof(std::uint64_t), 0U);

    void* Region()
    {
        return memory_.data();
    }

    void WriteUint32(const std::size_t offset, const std::uint32_t value)
    {
        std::memcpy(reinterpret_cast<std::uint8_t*>(Region()) + offset, &value, sizeof(value));
    }

    SharedMemoryRegionDirectory Attach()
    {
        auto directory = SharedMemoryRegionDirectory::Attach(Region(), kRegionSize);
        EXPECT_TRUE(directory.has_value());
        return directory.value();
    }
};

TEST_F(SharedMemoryRegionDirectoryTest, DirectoryIsAlignedToObjectAlignment)
{
    // Then the directory takes a multiple of the object alignment, so that the first object is aligned
    EXPECT_EQ(SharedMemoryRegionDirectory::GetSize() % SharedMemoryRegionDirectory::kObjectAlignment, 0U);
    EXPECT_GT(SharedMemoryRegionDirectory::GetSize(), 0U);
}

TEST_F(SharedMemoryRegionDirectoryTest, AttachingToZeroedRegionInitializesEmptyDirectory)
{
    // Given a zeroed region
    // When attaching to it
    auto directory = Attach();

    // Then no object exists
    const auto result = directory.Open("/object");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kNoSuchFileOrDirectory);
}

TEST_F(SharedMemoryRegionDirectoryTest, AttachingFailsForInvalidArguments)
{
    // When attaching to no region or a region, which is too small for the directory
    // Then it fails
    EXPECT_FALSE(SharedMemoryRegionDirectory::Attach(nullptr, kRegionSize).has_value());
    const auto result = SharedMemoryRegionDirectory::Attach(Region(), SharedMemoryRegionDirectory::GetSize() - 1U);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kInvalidArgument);
}

TEST_F(SharedMemoryRegionDirectoryTest, CreatedObjectsAreAlignedAndPlacedBehindEachOther)
{
    auto directory = Attach();

    // When creating two objects
    const auto first = directory.Create("/first", 100U);
    const auto second = directory.Create("/second", SharedMemoryRegionDirectory::kObjectAlignment + 1U);
    const auto third = directory.Create("/third", 1U);

    // Then the first one is located directly behind the directory and the next ones behind their predecessors,
    // aligned to the object alignment
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(first.value().offset, SharedMemoryRegionDirectory::GetSize());
    EXPECT_EQ(first.value().size, 100U);
    EXPECT_EQ(second.value().offset, first.value().offset + SharedMemoryRegionDirectory::kObjectAlignment);
    EXPECT_EQ(second.value().size, SharedMemoryRegionDirectory::kObjectAlignment + 1U);
    EXPECT_EQ(third.value().offset, second.value().offset + (2U * SharedMemoryRegionDirectory::kObjectAlignment));
}

TEST_F(SharedMemoryRegionDirectoryTest, OpenReturnsTheCreatedObject)
{
    auto directory = Attach();
    const auto created = directory.Create("/object", 4711U);
    ASSERT_TRUE(created.has_value());

    // When opening the object
    const auto opened = directory.Open("/object");

    // Then its location is returned
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value().offset, created.value().offset);
    EXPECT_EQ(opened.value().size, 4711U);
}

TEST_F(SharedMemoryRegionDirectoryTest, AnotherUserOfTheRegionSeesTheCreatedObjects)
{
    // Given an object created by one user of the region
    auto directory = Attach();
    const auto created = directory.Create("/object", 4711U);
    ASSERT_TRUE(created.has_value());

    // When another user attaches to the (already initialized) region
    auto other_directory = Attach();

    // Then it finds the object
    const auto opened = other_directory.Open("/object");
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value().offset, created.value().offset);
    EXPECT_EQ(opened.value().size, created.value().size);
}

TEST_F(SharedMemoryRegionDirectoryTest, CreatingAnExistingObjectReusesItIfItIsLargeEnough)
{
    // Given an existing object
    auto directory = Attach();
    const auto created = directory.Create("/object", 4711U);
    ASSERT_TRUE(created.has_value());

    // When creating it again with another size, which fits into the space reserved for it
    const auto recreated = directory.Create("/object", SharedMemoryRegionDirectory::kObjectAlignment);

    // Then it is reused with the new size
    ASSERT_TRUE(recreated.has_value());
    EXPECT_EQ(recreated.value().offset, created.value().offset);
    EXPECT_EQ(recreated.value().size, SharedMemoryRegionDirectory::kObjectAlignment);
    EXPECT_EQ(directory.Open("/object").value().size, SharedMemoryRegionDirectory::kObjectAlignment);
}

TEST_F(SharedMemoryRegionDirectoryTest, CreatingAnExistingObjectFailsIfItIsTooSmall)
{
    // Given an existing object
    auto directory = Attach();
    ASSERT_TRUE(directory.Create("/object", 4711U).has_value());

    // When creating it again with a size, which exceeds the space reserved for it
    const auto recreated = directory.Create("/object", SharedMemoryRegionDirectory::kObjectAlignment + 1U);

    // Then it fails
    ASSERT_FALSE(recreated.has_value());
    EXPECT_EQ(recreated.error(), Error::Code::kNoSpaceLeftOnDevice);
}

TEST_F(SharedMemoryRegionDirectoryTest, CreatingFailsIfTheRegionIsFull)
{
    // Given a region, which only has space for a single object behind the directory
    auto directory_result = SharedMemoryRegionDirectory::Attach(
        Region(), SharedMemoryRegionDirectory::GetSize() + SharedMemoryRegionDirectory::kObjectAlignment);
    ASSERT_TRUE(directory_result.has_value());
    auto directory = directory_result.value();
    ASSERT_TRUE(directory.Create("/first", 1U).has_value());

    // When creating another object
    const auto result = directory.Create("/second", 1U);

    // Then it fails
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kNotEnoughSpace);
}

TEST_F(SharedMemoryRegionDirectoryTest, CreatingFailsIfAllEntriesAreUsed)
{
    // Given a directory of a region, which has space for more objects, than the directory has entries
    constexpr auto kSpaceForAllEntries =
        (SharedMemoryRegionDirectory::kMaxNumberOfObjects + 1U) * SharedMemoryRegionDirectory::kObjectAlignment;
    auto directory_result =
        SharedMemoryRegionDirectory::Attach(Region(), SharedMemoryRegionDirectory::GetSize() + kSpaceForAllEntries);
    ASSERT_TRUE(directory_result.has_value());
    auto directory = directory_result.value();

    // and whose entries are all used
    for (std::size_t index = 0U; index < SharedMemoryRegionDirectory::kMaxNumberOfObjects; ++index)
    {
        ASSERT_TRUE(directory.Create("/object" + std::to_string(index), 1U).has_value());
    }

    // When creating another object
    const auto result = directory.Create("/one_too_many", 1U);

    // Then it fails
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kNoSpaceLeftOnDevice);
}

TEST_F(SharedMemoryRegionDirectoryTest, CreatingFailsForInvalidNamesAndSizes)
{
    auto directory = Attach();
    const std::string too_long_name(SharedMemoryRegionDirectory::kMaxNameLength, 'a');

    // When creating objects with an empty name, a name without space for the terminating null character or zero size
    // Then it fails
    EXPECT_EQ(directory.Create("", 1U).error(), Error::Code::kInvalidArgument);
    EXPECT_EQ(directory.Create(too_long_name, 1U).error(), Error::Code::kFilenameTooLong);
    EXPECT_EQ(directory.Create("/object", 0U).error(), Error::Code::kInvalidArgument);
    EXPECT_EQ(directory.Create("/object", std::numeric_limits<std::uint64_t>::max()).error(),
              Error::Code::kInvalidArgument);
    EXPECT_EQ(directory.Open(too_long_name).error(), Error::Code::kFilenameTooLong);
}

TEST_F(SharedMemoryRegionDirectoryTest, LongestValidNameCanBeUsed)
{
    auto directory = Attach();
    const std::string longest_name(SharedMemoryRegionDirectory::kMaxNameLength - 1U, 'a');

    // When creating an object with the longest valid name
    // Then it can be opened again
    ASSERT_TRUE(directory.Create(longest_name, 1U).has_value());
    EXPECT_TRUE(directory.Open(longest_name).has_value());
}

TEST_F(SharedMemoryRegionDirectoryTest, AttachingFailsForADifferentRegionSize)
{
    // Given a directory initialized for a region size
    score::cpp::ignore = Attach();

    // When attaching with another region size, e.g. due to an inconsistent configuration of the VMs
    const auto result = SharedMemoryRegionDirectory::Attach(Region(), kRegionSize * 2U);

    // Then it fails
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kInvalidArgument);
}

TEST_F(SharedMemoryRegionDirectoryTest, AttachingFailsForACorruptedDirectory)
{
    // Given a directory, whose magic number got corrupted
    score::cpp::ignore = Attach();
    WriteUint32(kMagicOffset, 0xBADU);

    // When attaching to it
    const auto result = SharedMemoryRegionDirectory::Attach(Region(), kRegionSize);

    // Then it fails
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kInvalidArgument);
}

TEST_F(SharedMemoryRegionDirectoryTest, CorruptedNumberOfObjectsIsBoundedByTheDirectorySize)
{
    // Given a directory, whose number of objects got corrupted
    auto directory = Attach();
    WriteUint32(kNumberOfObjectsOffset, std::numeric_limits<std::uint32_t>::max());

    // When looking up an object
    const auto result = directory.Open("/object");

    // Then only the entries within the directory are searched
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Code::kNoSuchFileOrDirectory);
    // and no object can be added anymore
    EXPECT_EQ(directory.Create("/object", 1U).error(), Error::Code::kNoSpaceLeftOnDevice);
}

TEST_F(SharedMemoryRegionDirectoryTest, AccessFailsIfTheDirectoryStaysLocked)
{
    // Given a directory, which stays locked, e.g. since its user died while holding the lock
    auto directory = Attach();
    WriteUint32(kLockOffset, 1U);

    // When accessing it
    // Then it fails after a timeout
    EXPECT_EQ(directory.Create("/object", 1U).error(), Error::Code::kKernelTimeout);
    EXPECT_EQ(directory.Open("/object").error(), Error::Code::kKernelTimeout);
}

}  // namespace
}  // namespace score::memory::shared
