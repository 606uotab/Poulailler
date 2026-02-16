set(LOCAL_INC "${CMAKE_SOURCE_DIR}/.local-deps/include")
set(LOCAL_LIB "${CMAKE_SOURCE_DIR}/.local-deps/lib")

add_executable(mc-tui
    src/main.c
    src/client.c
    src/ui.c
    src/ui_panels.c
)

target_include_directories(mc-tui PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/deps/cjson
    ${LOCAL_INC}
    ${LOCAL_INC}/ncursesw
)

target_link_directories(mc-tui PRIVATE ${LOCAL_LIB})

target_link_libraries(mc-tui PRIVATE
    cjson
    curl
    ncursesw
    panelw
    tinfo
    m
)
