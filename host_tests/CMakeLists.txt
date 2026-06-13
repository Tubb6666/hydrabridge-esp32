# Host-side unit tests. Plain CMake + Unity (sourced from ESP-IDF).
# Run on the Linux host with no ESP32 hardware. Each Phase 1+ module
# that has pure-C logic adds a tests/test_<module>.c here.
#
# Build:
#   cmake -S host_tests -B host_tests/build && cmake --build host_tests/build
#   ./host_tests/build/host_tests

cmake_minimum_required(VERSION 3.16)
project(hydra_ble_controller_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH not set. Source ~/esp/esp-idf/export.sh first.")
endif()

set(UNITY_SRC_DIR "$ENV{IDF_PATH}/components/unity/unity/src")
if(NOT EXISTS "${UNITY_SRC_DIR}/unity.c")
    message(FATAL_ERROR "Unity source not found at ${UNITY_SRC_DIR}")
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter -O0 -g)

# Collect every tests/test_*.c that has been added so far.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.c")

# Project sources that are pure-C and host-testable. Each Phase adds
# the .c files it owns to this list. Anything that pulls in esp-idf
# headers must be #ifdef ESP_PLATFORM-guarded inside the source.
set(MODULE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/event_log/src/event_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/config_store/src/config_store_defaults.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/fsci_codec/src/fsci_codec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/channel_model/src/channel_model.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/hydra64hd_protocol/src/hydra64hd_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/preset_engine/src/preset_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/light_registry/src/light_registry.c
)

add_executable(host_tests
    runner.c
    ${UNITY_SRC_DIR}/unity.c
    ${TEST_SOURCES}
    ${MODULE_SOURCES}
)

target_include_directories(host_tests PRIVATE
    ${UNITY_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/event_log/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/config_store/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/fsci_codec/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/channel_model/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/hydra64hd_protocol/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/preset_engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/light_registry/include
)

# Unity config: skip the unity_config.h include (ESP-IDF supplies one only
# inside its build system), keep colored stdout.
target_compile_definitions(host_tests PRIVATE
    UNITY_EXCLUDE_CONFIG_H
    UNITY_OUTPUT_COLOR=1
)
