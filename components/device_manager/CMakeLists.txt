find_package(Python3 REQUIRED COMPONENTS Interpreter)

idf_component_register(
    SRCS "src/DeviceManager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos driver esp_partition
)

# Автоинкремент build-номера в include/version.h на каждой сборке
add_custom_target(bump_version ALL
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bump_version.py
    BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/include/version.h
    COMMENT "Bumping firmware build number"
    VERBATIM)
add_dependencies(${COMPONENT_TARGET} bump_version)