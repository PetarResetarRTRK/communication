# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//quality/unit_testing:unit_testing.bzl", "cc_unit_test")
load("//score/mw:common_features.bzl", "COMPILER_WARNING_FEATURES")

cc_library(
    name = "shared_memory_region",
    srcs = [
        "shared_memory_region.cpp",
        "shared_memory_region_directory.cpp",
    ],
    hdrs = [
        "shared_memory_region.h",
        "shared_memory_region_directory.h",
    ],
    features = COMPILER_WARNING_FEATURES,
    implementation_deps = [
        "@score_baselibs//score/mw/log",
        "@score_baselibs//score/os:errno_logging",
        "@score_baselibs//score/os:fcntl",
        "@score_baselibs//score/os:mman",
        "@score_baselibs//score/os:stat",
        "@score_baselibs//score/os:unistd",
    ],
    tags = ["FFI"],
    visibility = ["//visibility:public"],
    deps = [
        "//score/memory/shared/typedshm/typedshm_wrapper:typedmemory",
        "@score_baselibs//score/language/futurecpp",
        "@score_baselibs//score/os:errno",
    ],
)

cc_unit_test(
    name = "shared_memory_region_directory_test",
    srcs = ["shared_memory_region_directory_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//score/memory:__pkg__"],
    deps = [":shared_memory_region"],
)

cc_unit_test(
    name = "shared_memory_region_test",
    srcs = ["shared_memory_region_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//score/memory:__pkg__"],
    deps = [
        ":shared_memory_region",
        "//score/memory/shared",
        "@score_baselibs//score/os:fcntl",
        "@score_baselibs//score/os:mman",
        "@score_baselibs//score/os:stat",
        "@score_baselibs//score/os:unistd",
    ],
)
