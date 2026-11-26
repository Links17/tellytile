# Copyright (c) 2026 CodeCraft AI Hardware contributors.
# SPDX-License-Identifier: Apache-2.0
# Each native example owns an explicit BSP composition. There is no implicit
# all-peripheral fallback: an exported project carries only its chosen inputs.
idf_build_get_property(codecraft_project_dir PROJECT_DIR)
include("${codecraft_project_dir}/bsp.cmake")
if(NOT CODECRAFT_BSP_SOURCES OR NOT CODECRAFT_BSP_REQUIRES)
    message(FATAL_ERROR "The project must declare its BSP sources and requirements")
endif()
# Register from the BSP CMakeLists.txt so ESP-IDF keeps its component directory.
