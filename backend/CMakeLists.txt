find_package(PkgConfig REQUIRED)

# Local deps paths
set(LOCAL_INC "${CMAKE_SOURCE_DIR}/.local-deps/include")
set(LOCAL_LIB "${CMAKE_SOURCE_DIR}/.local-deps/lib")

# System libraries via pkg-config (use local .pc files)
set(ENV{PKG_CONFIG_PATH} "${LOCAL_LIB}/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(XML2    REQUIRED libxml-2.0)

find_package(Threads REQUIRED)

# Vendored deps
add_library(tomlc99 STATIC deps/tomlc99/toml.c)
target_include_directories(tomlc99 PUBLIC deps/tomlc99)

add_library(cjson STATIC deps/cjson/cJSON.c)
target_include_directories(cjson PUBLIC deps/cjson)

# Backend daemon
set(BACKEND_SOURCES
    src/main.c
    src/error.c
    src/log.c
    src/config.c
    src/db.c
    src/models.c
    src/fetch_rss.c
    src/fetch_rest.c
    src/fetch_ws.c
    src/scheduler.c
    src/api_http.c
    src/api_unix.c
)

add_executable(mc-daemon ${BACKEND_SOURCES})

target_include_directories(mc-daemon PRIVATE
    include
    ${LOCAL_INC}
    ${LOCAL_INC}/ncursesw
    ${SQLITE3_INCLUDE_DIRS}
    ${XML2_INCLUDE_DIRS}
)

target_link_directories(mc-daemon PRIVATE ${LOCAL_LIB})

target_link_libraries(mc-daemon PRIVATE
    tomlc99
    cjson
    ${SQLITE3_LIBRARIES}
    ${XML2_LIBRARIES}
    curl
    microhttpd
    websockets
    Threads::Threads
    m
)
