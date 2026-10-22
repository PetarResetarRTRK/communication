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
#include "score/memory/shared/typedshm/typedshm_wrapper/test/typed_memory_mock.h"

#include <cerrno>

namespace score::memory::shared
{

TypedMemoryMock::TypedMemoryMock()
{
    ON_CALL(*this, CreateNamedRegionObject(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(score::cpp::make_unexpected(score::os::Error::createFromErrno(ENOTSUP))));
    ON_CALL(*this, OpenNamedRegionObject(::testing::_))
        .WillByDefault(::testing::Return(score::cpp::make_unexpected(score::os::Error::createFromErrno(ENOTSUP))));
}

}  // namespace score::memory::shared
