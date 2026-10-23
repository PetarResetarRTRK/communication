<!--
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

# Inter-VM Shared Memory Region

`SharedMemoryRegion` lets processes on different VMs share named shm-objects. It places all of them into a single
shared memory region, which the hypervisor shares between the VMs (e.g. a region of the QNX hypervisor's `vdev shmem`).

## How it is used

`SharedMemoryRegion` is a `TypedMemory` provider, which is installed as inter-VM memory provider of the
`SharedMemoryFactory` (`SharedMemoryFactory::SetInterVMMemoryProvider()`). The factory hands shm-objects with the
inter-VM path prefix (`/intervm-shared-shmem/`) to it. `mw::com` installs it in every process, whose configuration
contains `global/interVmSharedMemory`, and uses the prefix for all shm-objects of service instances with
`interVmSupport` (see the [mw::com configuration](../../../mw/com/impl/configuration/README.md#intervmsharedmemory)).

For such shm-objects, `SharedMemoryResource` doesn't use `shm_open()`: It asks the provider for the location of the
shm-object within the region (`TypedMemory::CreateNamedRegionObject()` / `OpenNamedRegionObject()`), i.e. a file
descriptor of the region and an offset and size, and maps exactly this part of the region. Providers, which don't
support this (e.g. typed memory), return `kOperationNotSupported`, so shm-objects are created as before.

## Region layout

```
offset 0                           GetSize()              GetSize() + 64 KiB
| directory (header + 256 entries) | shm-object 1 ...       | shm-object 2 ... | ...
```

- The region starts with a `SharedMemoryRegionDirectory`, which maps the names of the shm-objects to their offset and
  size within the region. It consists of fixed width types only, so that VMs with different OSes (e.g. QNX and Linux)
  share the same layout.
- The first user of a (zero-initialized) region initializes the directory. Every user checks, that the directory
  matches the configured region size.
- shm-objects are placed one after the other behind the directory. Each one is aligned to 64 KiB, which is a multiple
  of all page sizes in use (4, 16 and 64 KiB), so that each VM can map each shm-object on its own.
- The directory is protected by a spin lock within the region, since OS specific mutexes can't be shared between VMs.
  It is only held for a lookup or a single update. A user, which doesn't get it within one second, fails with
  `ETIMEDOUT`.
- shm-objects are never freed. Creating a shm-object, which already exists, reuses it, if it is large enough. Changing
  the sizes of shm-objects therefore requires a fresh region.

## Platform requirements

- The region is accessible via a file on each VM, which supports `mmap()` with offsets. The region may start at an
  offset within the file, which has to be a multiple of the page size:
  - On the hypervisor host (e.g. QNX), the POSIX shm-object of the region. With the QNX hypervisor, the region can be
    created when the VM starts with `vdev shmem create <name>,<size>[,mode[,gid]]`. On the host, it is
    `/dev/shmem/QVMSHM-<name>` (`subst` changes the name), and it starts with the region's 4 KiB control page, so the
    region starts at offset 4096.
  - Within a guest (e.g. Linux), the device node of the driver of the shared memory device.
- The region has to be zero-initialized, when it is created, and its size has to be configured identically on all VMs.
- The region has to be mapped cacheable and coherent on all VMs, since the directory and the shm-objects (e.g. the
  control segments of LoLa) are accessed with atomic operations from all VMs.
- All processes using the region need read/write access to the file (e.g. via the mode/group of the shm-object or a
  udev rule for the device node).

## Limitations

- The hypervisor's shared memory device has no read-only mapping. So every VM can write the whole region, including
  the shm-objects of providers on other VMs.
- The region has a fixed size and at most 256 shm-objects.
- If a user dies while holding the directory lock, the directory can't be accessed anymore until the region is reset.
