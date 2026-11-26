# SPDX-FileCopyrightText: 2026 Seeed Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0
# GX/DX source: Device reference/display-variants.md, pinned vendor Kconfig/BSP.
# The Target owns the build default. Export projection changes it to the
# selected profile, so direct native builds agree with the exported lock.
# A build default is not physical panel detection.
if(NOT DEFINED CODECRAFT_HARDWARE_VARIANT)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/target.json" codecraft_target_json)
    string(JSON codecraft_default_variant GET "${codecraft_target_json}" default_variant)
    set(CODECRAFT_HARDWARE_VARIANT "${codecraft_default_variant}" CACHE STRING "Declared Indicator display assembly: gx or dx")
endif()
if(NOT CODECRAFT_HARDWARE_VARIANT MATCHES "^(gx|dx)$")
    message(FATAL_ERROR "Unknown Indicator display assembly; choose gx or dx from sourced hardware identity")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/variants/${CODECRAFT_HARDWARE_VARIANT}.cmake")
