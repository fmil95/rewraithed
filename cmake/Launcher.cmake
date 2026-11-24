# rewraithed-launcher: a small Windows dialog that edits rewraithed.ini and
# starts the game. Built next to rewraithed.exe.

if(NOT WIN32)
    return()
endif()

add_executable(rewraithed-launcher WIN32
    "${PORT_DIR}/launcher/launcher.c"
    "${PORT_DIR}/launcher/controls.c"
    "${PORT_DIR}/launcher/joystick.c"
    "${PORT_DIR}/launcher/qols.c"
    "${PORT_DIR}/launcher/earlier.c"
    "${PORT_DIR}/launcher/mods_window.c"
    "${PORT_DIR}/launcher/update.c"
    "${PORT_DIR}/shared/settings.c"
    "${PORT_DIR}/shared/dc_disc.c"
    "${PORT_DIR}/shared/proto_disc.c"
    "${PORT_DIR}/shared/proto_areas.c"
    "${PORT_DIR}/shared/proto_convert.c"
    "${PORT_DIR}/shared/pc_disc.c"
    "${PORT_DIR}/shared/mod_list.c"
    "${PORT_DIR}/shared/mod_folders.c"
    "${PORT_DIR}/shared/sr2_disc.c"
    "${PORT_DIR}/res/rewraithed.rc"
)

target_include_directories(rewraithed-launcher PRIVATE "${PORT_DIR}/shared" "${PORT_DIR}/launcher")
target_compile_definitions(rewraithed-launcher PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE
    REWRAITHED_VERSION=L"${REWRAITHED_VERSION}")
target_link_libraries(rewraithed-launcher PRIVATE comctl32 winhttp advapi32)

# the game and the launcher go together
add_dependencies(rewraithed-launcher rewraithed)
