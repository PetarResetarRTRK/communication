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
#ifndef SCORE_MW_COM_GATEWAY_TRANSPORT_LAYER_SAMPLE_I_BIDIRECTIONAL_TRANSPORT_H_
#define SCORE_MW_COM_GATEWAY_TRANSPORT_LAYER_SAMPLE_I_BIDIRECTIONAL_TRANSPORT_H_

#include "score/mw/com/gateway/transport_layer/sample/messages/gateway_messages.h"
#include "score/result/result.h"

#include <score/callback.hpp>

#include <memory>

namespace score::mw::com::gateway
{

/// \brief Interface of BidirectionalTransport, introduced only for testing/mocking purposes
class IBidirectionalTransport
{
  public:
    using MessageHandler = score::cpp::callback<void(std::unique_ptr<TransportMessage>), 64>;
    /// \brief Callback, which is informed about a change of the connection state: true, when the connection has been
    ///        (re-)established, false, when it has been lost.
    using ConnectionStateHandler = score::cpp::callback<void(bool), 64>;

    virtual ~IBidirectionalTransport() = default;

    virtual score::Result<void> Setup() = 0;
    virtual void Shutdown() = 0;

    virtual bool IsConnected() const = 0;

    virtual score::Result<void> SendRequest(TransportMessage& message) = 0;
    virtual score::Result<void> SendNotification(TransportMessage& message) = 0;

    virtual void SetMessageHandler(MessageHandler handler) = 0;
    /// \brief Sets a callback, which is called on each change of the connection state. It is called in order with and
    ///        in the same context as the message handler, so that it is serialized with the handling of the messages.
    ///        Has to be set before Setup(). If not set, connection state changes are not reported.
    virtual void SetConnectionStateHandler(ConnectionStateHandler handler) = 0;
};

}  // namespace score::mw::com::gateway

#endif  // SCORE_MW_COM_GATEWAY_TRANSPORT_LAYER_SAMPLE_I_BIDIRECTIONAL_TRANSPORT_H_
