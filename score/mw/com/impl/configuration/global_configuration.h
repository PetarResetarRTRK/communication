/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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
#ifndef SCORE_MW_COM_IMPL_CONFIGURATION_GLOBAL_CONFIGURATION_H
#define SCORE_MW_COM_IMPL_CONFIGURATION_GLOBAL_CONFIGURATION_H

#include "score/mw/com/impl/configuration/quality_type.h"
#include "score/mw/com/impl/configuration/shm_size_calc_mode.h"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace score::mw::com::impl
{

/// \brief Configuration of the shared memory region, in which the shm-objects of all service instances with
///        interVmSupport are placed, so that they are shared between VMs.
struct InterVmSharedMemoryConfiguration
{
    /// \brief Index of a VM sharing the region.
    using VmIndex = std::uint8_t;

    /// \brief Maximum number of VMs, which share a region.
    static constexpr std::size_t kMaxNumberOfVms{2U};

    /// \brief Path of the file, which gives access to the region on this VM.
    std::string region_path;
    /// \brief Size of the region in bytes. It has to be the same on all VMs.
    std::uint64_t region_size;
    /// \brief Index of this VM. Each VM sharing the region needs a distinct one (smaller than kMaxNumberOfVms).
    VmIndex vm_index;
};

class GlobalConfiguration final
{
  public:
    using ApplicationId = std::uint32_t;

    // default value for ASIL-QM and ASIL-B receive message queue sizes.
    //
    // False positive: This value is used in teh GlobalConfiguration constructor.
    // coverity[autosar_cpp14_a0_1_1_violation: FALSE]
    static constexpr std::int32_t DEFAULT_MIN_NUM_MESSAGES_RX_QUEUE{10};
    // default value for ASIL-B send message queue sizes.
    //
    // False positive: This value is used in teh GlobalConfiguration constructor.
    // coverity[autosar_cpp14_a0_1_1_violation: FALSE]
    static constexpr std::int32_t DEFAULT_MIN_NUM_MESSAGES_TX_QUEUE{20};

    GlobalConfiguration() noexcept;

    /**
     * \brief Class is only move constructible
     */
    GlobalConfiguration(const GlobalConfiguration& other) = delete;
    GlobalConfiguration(GlobalConfiguration&& other) noexcept = default;
    GlobalConfiguration& operator=(const GlobalConfiguration& other) & = delete;
    GlobalConfiguration& operator=(GlobalConfiguration&& other) & noexcept = default;

    ~GlobalConfiguration() noexcept = default;

    void SetProcessAsilLevel(const QualityType process_asil_level)
    {
        process_asil_level_ = process_asil_level;
    }

    void SetReceiverMessageQueueSize(const QualityType quality_type, const std::int32_t queue_size) noexcept;

    void SetSenderMessageQueueSize(const std::int32_t queue_size) noexcept
    {
        message_tx_queue_size_b = queue_size;
    }

    void SetShmSizeCalcMode(const ShmSizeCalculationMode shm_size_calc_mode) noexcept;

    std::int32_t GetReceiverMessageQueueSize(const QualityType quality_type) const noexcept;

    std::int32_t GetSenderMessageQueueSize() const noexcept
    {
        return message_tx_queue_size_b;
    }

    QualityType GetProcessAsilLevel() const
    {
        return process_asil_level_;
    }

    void SetApplicationId(const ApplicationId application_id)
    {
        application_id_ = application_id;
    }

    std::optional<ApplicationId> GetApplicationId() const
    {
        return application_id_;
    }

    ShmSizeCalculationMode GetShmSizeCalcMode() const noexcept
    {
        return shm_size_calc_mode_;
    }

    void SetInterVmSharedMemory(InterVmSharedMemoryConfiguration inter_vm_shared_memory) noexcept
    {
        inter_vm_shared_memory_ = std::move(inter_vm_shared_memory);
    }

    /// \brief Returns the configuration of the shared memory region shared between VMs, if there is one.
    const std::optional<InterVmSharedMemoryConfiguration>& GetInterVmSharedMemory() const noexcept
    {
        return inter_vm_shared_memory_;
    }

  private:
    /// properties/settings from the "global" section
    QualityType process_asil_level_;
    std::optional<std::uint32_t> application_id_;

    std::int32_t message_rx_queue_size_qm;
    std::int32_t message_rx_queue_size_b;
    std::int32_t message_tx_queue_size_b;

    ShmSizeCalculationMode shm_size_calc_mode_;

    std::optional<InterVmSharedMemoryConfiguration> inter_vm_shared_memory_;
};

}  // namespace score::mw::com::impl

#endif  // SCORE_MW_COM_IMPL_CONFIGURATION_GLOBAL_CONFIGURATION_H
