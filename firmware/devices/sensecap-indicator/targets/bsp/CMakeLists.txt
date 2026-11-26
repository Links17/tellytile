# Copyright (c) 2026 CodeCraft AI Hardware contributors.
# SPDX-License-Identifier: Apache-2.0
include("${CMAKE_CURRENT_LIST_DIR}/../../../../platforms/esp-idf/cmake/bsp.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../variants.cmake")
idf_component_register(
    SRCS ${CODECRAFT_BSP_SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES ${CODECRAFT_BSP_REQUIRES}
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${CODECRAFT_VARIANT_DEFINES})
