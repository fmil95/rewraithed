# rewraithed-launcher: a small Windows dialog that edits rewraithed.ini and
# starts the game. Built next to rewraithed.exe.

if(NOT WIN32)
    return()
endif()

add_executable(rewraithed-launcher WIN32
    "${PORT_DIR}/launcher/launcher.c"
    "${PORT_DIR}/launcher/controls.c"
    "${PORT_DIR}/shared/settings.c"
)

target_include_directories(rewraithed-launcher PRIVATE "${PORT_DIR}/shared" "${PORT_DIR}/launcher")
target_compile_definitions(rewraithed-launcher PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
target_link_libraries(rewraithed-launcher PRIVATE comctl32)

# the game and the launcher go together
add_dependencies(rewraithed-launcher rewraithed)
