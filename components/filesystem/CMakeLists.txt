cmake_minimum_required(VERSION 3.16)

# ---- Source files of the bundled esp_littlefs ----
set(LITTLEFS_SOURCES
    "esp_littlefs/src/esp_littlefs.c"
    "esp_littlefs/src/littlefs_esp_part.c"
    "esp_littlefs/src/lfs_config.c"
    "esp_littlefs/src/littlefs/lfs.c"
    "esp_littlefs/src/littlefs/lfs_util.c"
)

# SDMMC support is opt-in via CONFIG_LITTLEFS_SDMMC_SUPPORT (menuconfig)
if(CONFIG_LITTLEFS_SDMMC_SUPPORT)
    list(APPEND LITTLEFS_SOURCES "esp_littlefs/src/littlefs_sdmmc.c")
endif()

idf_component_register(
    SRCS "src/filesystem.cpp" ${LITTLEFS_SOURCES}
    INCLUDE_DIRS "include" "esp_littlefs/include"
    PRIV_INCLUDE_DIRS "esp_littlefs/src"
    REQUIRES esp_partition
    PRIV_REQUIRES spi_flash vfs esptool_py
)

# Optional SDMMC, if user enabled it in menuconfig
if(CONFIG_LITTLEFS_SDMMC_SUPPORT)
    idf_component_add_dependencies(REQUIRES sdmmc)
endif()

# lfs.c needs to know which "lfs_config.h" to include
# Must be done AFTER idf_component_register (target now exists).
set_source_files_properties(
    ${LITTLEFS_SOURCES}
    PROPERTIES COMPILE_FLAGS "-DLFS_CONFIG=lfs_config.h"
)

# Compile-time flags from menuconfig, matching the original esp_littlefs component
if(CONFIG_LITTLEFS_FCNTL_GET_PATH)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        -DF_GETPATH=${CONFIG_LITTLEFS_FCNTL_F_GETPATH_VALUE})
endif()

if(CONFIG_LITTLEFS_MULTIVERSION)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC -DLFS_MULTIVERSION)
endif()

if(CONFIG_LITTLEFS_MALLOC_STRATEGY_DISABLE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC -DLFS_NO_MALLOC)
endif()

if(NOT CONFIG_LITTLEFS_ASSERTS)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC -DLFS_NO_ASSERT)
endif()
