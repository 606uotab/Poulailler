cmake_minimum_required(VERSION 3.16)
project(MonitorCrebirth VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Local deps prefix (headers + libs extracted from deb packages)
set(LOCAL_DEPS "${CMAKE_SOURCE_DIR}/.local-deps")
list(APPEND CMAKE_PREFIX_PATH "${LOCAL_DEPS}")
set(ENV{PKG_CONFIG_PATH} "${LOCAL_DEPS}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

add_subdirectory(backend)
add_subdirectory(clients/tui)
