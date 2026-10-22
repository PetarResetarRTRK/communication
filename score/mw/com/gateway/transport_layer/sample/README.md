<!---- 
*******************************************************************************
Copyright (c) 2026 Contributors to the Eclipse Foundation

See the NOTICE file(s) distributed with this work for additional
information regarding copyright ownership.

This program and the accompanying materials are made available under the
terms of the Apache License Version 2.0 which is available at
https://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
*******************************************************************************
-->

# Gateway Transport Layer Sample Implementation

The functionality a `Gateway Transport Layer` has to provide can be roughly divided into two parts:

1. Provide a message transport between the gateways, so that the gateways instances can interact with specific control 
messages.
2. Provide the data exchange for the service-instance related data. In case of a non-copying shared memory setup, 
this means to make shared-memory accessible between VMs. In case of a copying `Transport Layer` this means setting up DMA 
data transfers and the like.


For the first part this module provides a sample implementation of a mw::com gateway's transport layer based on regular 
POSIX network sockets for communication. Since this communication mechanism should be available in most HyperVisor 
environments, we decided to add this as a sample implementation. 

## Vendor-specific implementation details

Our sample Transport Layer implementation returns `true` for an API call of `IsMemorySharingSupported`. So it is a
sample for a `Gateway Transport Layer` for an inter-VM setup, where memory sharing between VMs is possible.

The second part (see above), i.e. making shared-memory objects visible/accessible between gateway domains (VMs), is not
done by the Transport Layer: The hypervisor provides a shared memory region, into which every process on both VMs
places the shm-objects of service instances with `interVmSupport` (see
[score/memory/shared/intervm](../../../../../memory/shared/intervm/README.md) and
[Memory Sharing via an Inter-VM Shared Memory Region](../../README.md#memory-sharing-via-an-inter-vm-shared-memory-region)).
So the shm-objects created on the source side are already accessible on the destination side, and the `ProvideService`
request doesn't carry the sizes of the shm-objects.

The Transport Layer reports changes of its connection state to the gateway application (see
[Remote Gateway Connection](../../README.md#remote-gateway-connection)).
