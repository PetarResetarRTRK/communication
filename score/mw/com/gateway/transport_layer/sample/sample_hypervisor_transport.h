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
#ifndef SCORE_MW_COM_GATEWAY_TRANSPORT_LAYER_SAMPLE_HYPERVISOR_TRANSPORT_H_
#define SCORE_MW_COM_GATEWAY_TRANSPORT_LAYER_SAMPLE_HYPERVISOR_TRANSPORT_H_

#include "score/mw/com/gateway/gateway_application/gateway_core.h"
#include "score/mw/com/gateway/transport_layer/sample/i_bidirectional_transport.h"
#include "score/mw/com/gateway/transport_layer/transport.h"

#include <memory>

namespace score::mw::com::gateway
{

struct ShmPaths
{
    std::string control;
    std::string data;
};

struct ShmSizes
{
    std::uint32_t control;
    std::uint32_t data;
};

/// \brief Transport layer, which exchanges the messages between the gateways via the BidirectionalTransport (POSIX
///        sockets).
/// \details It supports memory sharing: The shm-objects of service instances with interVmSupport are placed into the
///          inter-VM shared memory region by the inter-VM memory provider of every process on both VMs (see
///          score/memory/shared/intervm and global/interVmSharedMemory in the mw::com configuration). So the
///          shm-objects created on the source VM are already accessible on the destination VM and the transport layer
///          doesn't have to make them accessible itself.
class SampleHyperVisorTransport : public Transport
{
  public:
    SampleHyperVisorTransport(GatewayCore& gateway_app, std::unique_ptr<IBidirectionalTransport> transport) noexcept;
    ~SampleHyperVisorTransport() override;

    SampleHyperVisorTransport(const SampleHyperVisorTransport&) = delete;
    SampleHyperVisorTransport& operator=(const SampleHyperVisorTransport&) = delete;
    SampleHyperVisorTransport(SampleHyperVisorTransport&&) = delete;
    SampleHyperVisorTransport& operator=(SampleHyperVisorTransport&&) = delete;

    bool IsMemorySharingSupported() const override;

    Result<void> Setup() override;
    void Shutdown() override;

    // Methods to create the related TransportMessage and call SendMessage of BidirectionalTransport

    Result<void> ProvideService(score::mw::com::InstanceSpecifier service_instance_specifier,
                                std::vector<score::mw::com::EventInfo> service_elements) override;
    Result<void> OfferService(score::mw::com::InstanceSpecifier service_instance_specifier) override;
    Result<void> StopOfferService(score::mw::com::InstanceSpecifier service_instance_specifier) override;

    Result<void> NotifyUpdate(score::mw::com::InstanceSpecifier service_instance_specifier,
                              impl::ServiceElementType updated_element_type,
                              std::string updated_element_name) override;
    Result<void> RegisterUpdateNotification(score::mw::com::InstanceSpecifier service_instance_specifier,
                                            impl::ServiceElementType element_type,
                                            std::string element_name) override;
    Result<void> UnregisterUpdateNotification(score::mw::com::InstanceSpecifier service_instance_specifier,
                                              impl::ServiceElementType element_type,
                                              std::string element_name) override;

  private:
    void HandleProvideServiceRequest(std::unique_ptr<TransportMessage> message);

    void HandleStopOfferServiceRequest(std::unique_ptr<TransportMessage> message);

    void HandleOfferServiceRequest(std::unique_ptr<TransportMessage> message);

    void HandleUpdateNotification(std::unique_ptr<TransportMessage> message);

    void HandleRegisterNotificationRequest(std::unique_ptr<TransportMessage> message);

    void HandleUnregisterNotificationRequest(std::unique_ptr<TransportMessage> message);
    /// \brief Internal callback for incoming messages from the transport.
    /// \details inspects the received message and dispatches it to related API calls of the gateway application
    /// (e.g. ProvideService, NotifyUpdate, etc.).
    /// @param message received message from the BidirectionalTransport (transport_ member variable)
    void OnMessageReceived(std::unique_ptr<TransportMessage> message);

    GatewayCore& gateway_app_;
    std::unique_ptr<IBidirectionalTransport> message_transport_;
};

}  // namespace score::mw::com::gateway

#endif  // SCORE_MW_COM_GATEWAY_TRANSPORT_LAYER_SAMPLE_HYPERVISOR_TRANSPORT_H_
