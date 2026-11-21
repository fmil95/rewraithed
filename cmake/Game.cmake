# The game: soul-re's decompiled sources compiled unmodified, plus the PC layer
# in port/. Nothing in soul-re is edited; all PC differences come from
#   - port/include/port/prefix.h  (force-included into every game file)
#   - port/overrides.txt          (functions replaced by PC versions)
#   - port/fixups.cmake           (the few textual fixes MSVC needs)
#   - port/src                    (PC implementations of the replaced pieces)
#
# Overrides and fixups are applied to copies under build/fixed/; that
# directory is searched before soul-re/src, so a patched copy also wins when
# the file is #included by another one.

# --- main executable sources, taken from soul-re's splat config ------------
# Every Game/<name> segment of SLUS_007.08.yaml (code or data) that has a
# src/Game/<name>.c is linked into the PSX executable, so the PC build takes
# exactly those. Files #included by others (RAZIEL/*, G2/ANM*) and the loose
# HASM.c are not segments and are skipped automatically.
set(GAME_SPLAT_CONFIG "${SOULRE_DIR}/config/splat/SLUS_007.08.yaml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GAME_SPLAT_CONFIG}")
file(STRINGS "${GAME_SPLAT_CONFIG}" _segment_lines REGEX "- \\[0x[0-9A-Fa-f]+, *[._a-z]+, *Game/")
set(GAME_FILES "")
foreach(_line IN LISTS _segment_lines)
    string(REGEX REPLACE ".*, *[._a-z]+, *(Game/[^] ,]+).*" "\\1" _seg "${_line}")
    if(EXISTS "${SOULRE_DIR}/src/${_seg}.c")
        list(APPEND GAME_FILES "${_seg}.c")
    endif()
endforeach()
list(REMOVE_DUPLICATES GAME_FILES)

# --- patched copies ---------------------------------------------------------
set(GAME_FIXED_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixed")
set(_patched_files "")
file(REMOVE_RECURSE "${GAME_FIXED_DIR}")

# Loads the current text of a soul-re file (patched so far, or original).
macro(_game_patch_load file)
    set(_src "${SOULRE_DIR}/src/${file}")
    set(_dst "${GAME_FIXED_DIR}/${file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_src}")
    if(EXISTS "${_dst}.pending")
        file(READ "${_dst}.pending" _text)
    else()
        file(READ "${_src}" _text)
    endif()
endmacro()

macro(_game_patch_store file)
    file(WRITE "${GAME_FIXED_DIR}/${file}.pending" "${_text}")
    set(_patched_files ${_patched_files} "${file}" PARENT_SCOPE)
endmacro()

# port/fixups.cmake: game_fixup(<file> FIND <text> REPLACE <text>)
function(game_fixup file)
    # PARSE_ARGV keeps ';' inside FIND/REPLACE text intact.
    cmake_parse_arguments(PARSE_ARGV 1 FX "" "FIND;REPLACE" "")
    _game_patch_load("${file}")
    string(FIND "${_text}" "${FX_FIND}" _pos)
    if(_pos EQUAL -1)
        message(WARNING "fixup no longer applies to ${file} (fixed upstream?): ${FX_FIND}")
        return()
    endif()
    string(REPLACE "${FX_FIND}" "${FX_REPLACE}" _text "${_text}")
    _game_patch_store("${file}")
endfunction()

# Renames only the definition of <func> to <func>_psx. Prototypes and call
# sites keep the original name, so they reach the PC version in port/src.
# A definition is a line starting at column 0 with "<type> <func>(" and no ';'.
# A prototype of the original name is left in its place, so later references
# in the same file still compile when no header declares it.
function(game_rename_definition file func)
    _game_patch_load("${file}")
    set(_re "\n([A-Za-z_][^\n;={}]*[ *])${func}([ \t]*\\([^\n;{]*\\))([ \t]*\n?[ \t]*{)")
    string(REGEX MATCHALL "${_re}" _hits "${_text}")
    list(LENGTH _hits _n)
    if(NOT _n EQUAL 1)
        message(WARNING "overrides.txt: expected one definition of ${func} in ${file}, found ${_n}")
        return()
    endif()
    string(REGEX REPLACE "${_re}" "\n\\1${func}\\2;\n\\1${func}_psx\\2\\3" _text "${_text}")
    _game_patch_store("${file}")
endfunction()

# port/fixups.cmake: psycross_fixup(<file under PsyCross/src> FIND .. REPLACE ..)
# Same idea for PsyCross: patched copies in build/fixed-psycross, used by
# cmake/PsyCross.cmake in place of the submodule's files.
set(PSYCROSS_FIXED_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixed-psycross")
set(_psycross_patched "")
file(REMOVE_RECURSE "${PSYCROSS_FIXED_DIR}")

function(psycross_fixup file)
    cmake_parse_arguments(PARSE_ARGV 1 FX "" "FIND;REPLACE" "")
    set(_src "${PSYCROSS_DIR}/src/${file}")
    set(_dst "${PSYCROSS_FIXED_DIR}/${file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_src}")
    if(EXISTS "${_dst}")
        file(READ "${_dst}" _text)
    else()
        file(READ "${_src}" _text)
    endif()
    string(FIND "${_text}" "${FX_FIND}" _pos)
    if(_pos EQUAL -1)
        message(WARNING "PsyCross fixup no longer applies to ${file} (fixed upstream?): ${FX_FIND}")
        return()
    endif()
    string(REPLACE "${FX_FIND}" "${FX_REPLACE}" _text "${_text}")
    file(WRITE "${_dst}" "${_text}")
    set(_psycross_patched ${_psycross_patched} "${file}" PARENT_SCOPE)
endfunction()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PORT_DIR}/fixups.cmake")
include("${PORT_DIR}/fixups.cmake")

# Inline MIPS assembly compiles to nothing (port/include/port/prefix.h), so a
# macro built on it silently does nothing. Each one in soul-re must have a C
# version in port/include/port/hasm_inline.h or be known to be harmless.
set(_asm_macros_harmless
    _break          # debugger breakpoint
    STACK_SET       # main loop on the scratchpad stack
    STACK_RESTORE
)
file(READ "${PORT_DIR}/include/port/hasm_inline.h" _asm_ported)
file(GLOB_RECURSE _asm_scan "${SOULRE_DIR}/include/Game/*.h" "${SOULRE_DIR}/include/Libs/SDK.h" "${SOULRE_DIR}/src/Game/*.c")
foreach(_f IN LISTS _asm_scan)
    file(STRINGS "${_f}" _defs REGEX "^[ \t]*#[ \t]*define[ \t]+[A-Za-z_0-9]+\\(.*(__asm__|asm)[ \t]*\\(")
    foreach(_d IN LISTS _defs)
        string(REGEX REPLACE "^[ \t]*#[ \t]*define[ \t]+([A-Za-z_0-9]+).*" "\\1" _name "${_d}")
        if(NOT _name IN_LIST _asm_macros_harmless AND NOT _asm_ported MATCHES "#define ${_name}\\(")
            file(RELATIVE_PATH _rel "${SOULRE_DIR}" "${_f}")
            message(WARNING "rewraithed: inline-asm macro ${_name} (soul-re/${_rel}) has no C version in port/include/port/hasm_inline.h")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _psycross_patched)

# --- port/overrides.txt -----------------------------------------------------
#   <file under soul-re/src>  <function>... [-D<define>...]
# Functions: the decomp's definition becomes <function>_psx and port/src (or
# the C runtime) provides <function>. -D entries add defines for that file.
# A file listed alone is dropped from the build.
set(GAME_OVERRIDES_FILE "${PORT_DIR}/overrides.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GAME_OVERRIDES_FILE}")
file(STRINGS "${GAME_OVERRIDES_FILE}" _override_lines REGEX "^[^#]")
set(_dropped_files "")
set(_file_defines "")
foreach(_line IN LISTS _override_lines)
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "")
        continue()
    endif()
    string(REGEX REPLACE "[ \t]+" ";" _parts "${_line}")
    list(GET _parts 0 _file)
    if(NOT EXISTS "${SOULRE_DIR}/src/${_file}")
        message(WARNING "overrides.txt: ${_file} no longer exists in soul-re")
        continue()
    endif()
    list(LENGTH _parts _n)
    if(_n EQUAL 1)
        list(APPEND _dropped_files "${_file}")
        continue()
    endif()
    list(SUBLIST _parts 1 -1 _items)
    foreach(_item IN LISTS _items)
        if(_item MATCHES "^-D(.+)$")
            list(APPEND _file_defines "${_file}|${CMAKE_MATCH_1}")
        else()
            game_rename_definition("${_file}" "${_item}")
        endif()
    endforeach()
endforeach()

# --- port/asm_functions.txt -------------------------------------------------
#   <file under soul-re/src>  <function>:<args>[:ret]  [<callee>:<args> ...]
# The decomp's definition is renamed and the original compiled function,
# from splat's asm/nonmatchings, is recompiled in its place (cmake/Hasm.cmake
# turns GAME_ASM_FUNCTIONS into generated sources).
set(GAME_ASM_FUNCTIONS_FILE "${PORT_DIR}/asm_functions.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GAME_ASM_FUNCTIONS_FILE}")
file(STRINGS "${GAME_ASM_FUNCTIONS_FILE}" _asmfn_lines REGEX "^[^#]")
set(GAME_ASM_FUNCTIONS "")
foreach(_line IN LISTS _asmfn_lines)
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "")
        continue()
    endif()
    string(REGEX REPLACE "[ \t]+" ";" _parts "${_line}")
    list(GET _parts 0 _file)
    list(GET _parts 1 _entry)
    string(REGEX REPLACE ":.*" "" _func "${_entry}")
    string(REGEX REPLACE "\\.c$" "" _stem "${_file}")
    set(_asm "${SOULRE_DIR}/asm/nonmatchings/${_stem}/${_func}.s")
    if(NOT EXISTS "${_asm}")
        message(WARNING "asm_functions.txt: ${_asm} not found (run splat in soul-re); keeping the decomp's C for ${_func}")
        continue()
    endif()
    game_rename_definition("${_file}" "${_func}")
    # one generated file per function: name|asm|entry|callees...
    string(REPLACE ";" "|" _spec "${_func};${_asm};${_parts}")
    list(APPEND GAME_ASM_FUNCTIONS "${_spec}")
endforeach()

list(REMOVE_DUPLICATES _patched_files)
foreach(_file IN LISTS _patched_files)
    file(RENAME "${GAME_FIXED_DIR}/${_file}.pending" "${GAME_FIXED_DIR}/${_file}")
endforeach()

# Some soul-re files are #included by another source rather than compiled
# (RAZIEL.c includes STEERING.c, ANIMG2.c its helpers). The include search
# finds a patched copy first, but the build only tracks what it read last
# time, so the including source is compiled from a copy in the patched tree
# as well: it is regenerated with the others and always sees the patches.
foreach(_file IN LISTS GAME_FILES)
    if("${_file}" IN_LIST _patched_files)
        continue()
    endif()
    file(STRINGS "${SOULRE_DIR}/src/${_file}" _incs REGEX "^[ \t]*#[ \t]*include[ \t]+\"Game/.*\\.c\"")
    foreach(_inc IN LISTS _incs)
        string(REGEX REPLACE ".*\"(Game/.*\\.c)\".*" "\\1" _inc "${_inc}")
        if("${_inc}" IN_LIST _patched_files)
            configure_file("${SOULRE_DIR}/src/${_file}" "${GAME_FIXED_DIR}/${_file}" COPYONLY)
            list(APPEND _patched_files "${_file}")
            break()
        endif()
    endforeach()
endforeach()

# Path actually compiled for a soul-re file (the patched copy if there is one).
function(game_source_path out file)
    if("${file}" IN_LIST _patched_files)
        set(${out} "${GAME_FIXED_DIR}/${file}" PARENT_SCOPE)
    else()
        set(${out} "${SOULRE_DIR}/src/${file}" PARENT_SCOPE)
    endif()
endfunction()

set(GAME_SRCS "")
foreach(_file IN LISTS GAME_FILES)
    if(NOT _file IN_LIST _dropped_files)
        game_source_path(_path "${_file}")
        list(APPEND GAME_SRCS "${_path}")
    endif()
endforeach()
list(LENGTH GAME_SRCS _n_game)
list(LENGTH _patched_files _n_patched)
message(STATUS "rewraithed: ${_n_game} game source files from soul-re (${_n_patched} patched)")

# --- overlays (port/overlays.txt) -------------------------------------------
# Listed overlays are linked statically; build/overlay_registry.c maps each
# overlay name to its C function table (the "const <table type> X = {" in its
# sources), which port/src/relmod.c installs when the game loads the .drm.
#
# On PSX every overlay is loaded at 0x88000000, so their address-named
# symbols (func_88xxxxxx, D_88xxxxxx, jtbl_88xxxxxx) repeat from one overlay
# to the next. Each overlay's sources are compiled with a generated header
# (build/overlays/<name>.h, force-included) that renames those symbols to
# ovl_<name>_<symbol>. The same goes for functions that several overlays
# each carry a copy of (e.g. FX_StartInstanceBurrow).
set(GAME_OVERLAYS_FILE "${PORT_DIR}/overlays.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GAME_OVERLAYS_FILE}")
file(STRINGS "${GAME_OVERLAYS_FILE}" _overlay_lines REGEX "^[^#]")

# first pass: functions defined at file scope (a definition starts in column
# 0 and has no ';' on its line) in more than one overlay
set(_ovl_all_defs "")
set(_ovl_shared_defs "")
foreach(_ovl IN LISTS _overlay_lines)
    string(STRIP "${_ovl}" _ovl)
    if(_ovl STREQUAL "")
        continue()
    endif()
    file(GLOB _ovl_srcs "${SOULRE_DIR}/src/Overlays/${_ovl}/*.c")
    set(_defs "")
    foreach(_f IN LISTS _ovl_srcs)
        file(STRINGS "${_f}" _lines REGEX "^[A-Za-z_][A-Za-z0-9_ \t*]*[ \t*]([A-Za-z_][A-Za-z0-9_]*)[ \t]*\\([^;]*$")
        foreach(_l IN LISTS _lines)
            if(_l MATCHES "^(static|typedef|extern|if|else|while|for|switch|return)[ \t(]")
                continue()
            endif()
            string(REGEX REPLACE "^[A-Za-z_][A-Za-z0-9_ \t*]*[ \t*]([A-Za-z_][A-Za-z0-9_]*)[ \t]*\\(.*" "\\1" _name "${_l}")
            list(APPEND _defs "${_name}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _defs)
    foreach(_name IN LISTS _defs)
        if("${_name}" IN_LIST _ovl_all_defs)
            list(APPEND _ovl_shared_defs "${_name}")
        else()
            list(APPEND _ovl_all_defs "${_name}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _ovl_shared_defs)

set(OVERLAY_SRCS "")
set(_reg_decls "")
set(_reg_entries "")
foreach(_ovl IN LISTS _overlay_lines)
    string(STRIP "${_ovl}" _ovl)
    if(_ovl STREQUAL "")
        continue()
    endif()
    # soul-re's sources, plus port/overlays/<name> for overlay data that
    # soul-re leaves to the PSX libraries linked into the overlay
    file(GLOB _ovl_found CONFIGURE_DEPENDS "${SOULRE_DIR}/src/Overlays/${_ovl}/*.c" "${PORT_DIR}/overlays/${_ovl}/*.c")
    if(NOT _ovl_found)
        message(WARNING "overlays.txt: no sources for overlay ${_ovl}")
        continue()
    endif()
    # soul-re files go through port/fixups.cmake like the game's own
    set(_ovl_srcs "")
    foreach(_f IN LISTS _ovl_found)
        file(RELATIVE_PATH _rel "${SOULRE_DIR}/src" "${_f}")
        if(_rel MATCHES "^Overlays/")
            game_source_path(_f "${_rel}")
        endif()
        list(APPEND _ovl_srcs "${_f}")
    endforeach()
    list(APPEND OVERLAY_SRCS ${_ovl_srcs})

    # renaming header for the address-named symbols the overlay uses
    file(GLOB _ovl_hdrs "${SOULRE_DIR}/include/Overlays/${_ovl}/*.h")
    set(_syms "")
    foreach(_f IN LISTS _ovl_srcs _ovl_hdrs)
        file(STRINGS "${_f}" _lines REGEX "(func|D|jtbl)_88[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]")
        foreach(_l IN LISTS _lines)
            string(REGEX MATCHALL "(func|D|jtbl)_88[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]" _m "${_l}")
            list(APPEND _syms ${_m})
        endforeach()
        if(_ovl_shared_defs)
            file(READ "${_f}" _t)
            foreach(_name IN LISTS _ovl_shared_defs)
                if(_t MATCHES "[^A-Za-z0-9_]${_name}[ \t]*\\(")
                    list(APPEND _syms "${_name}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _syms)
    list(SORT _syms)
    set(_hdr "/* Generated by cmake/Game.cmake: overlay ${_ovl}'s address-named symbols. */\n")
    foreach(_sym IN LISTS _syms)
        string(APPEND _hdr "#define ${_sym} ovl_${_ovl}_${_sym}\n")
    endforeach()
    set(_hdr_path "${CMAKE_CURRENT_BINARY_DIR}/overlays/${_ovl}.h")
    file(CONFIGURE OUTPUT "${_hdr_path}" CONTENT "${_hdr}" @ONLY)
    if(MSVC)
        set_property(SOURCE ${_ovl_srcs} APPEND PROPERTY COMPILE_OPTIONS "/FI${_hdr_path}")
    else()
        set_property(SOURCE ${_ovl_srcs} APPEND PROPERTY COMPILE_OPTIONS -include "${_hdr_path}")
    endif()

    set(_table "")
    foreach(_f IN LISTS _ovl_srcs)
        file(READ "${_f}" _t)
        string(REGEX MATCH "const[ \t]+(MonsterFunctionTable|mcmenu_table_t|cinema_fn_table_t)[ \t]+([A-Za-z0-9_]+)[ \t]*=" _m "${_t}")
        if(_m)
            set(_type "${CMAKE_MATCH_1}")
            set(_table "${CMAKE_MATCH_2}")
            if("${_table}" IN_LIST _syms)
                set(_table "ovl_${_ovl}_${_table}")
            endif()
            string(APPEND _reg_decls "extern const ${_type} ${_table};\n")
            string(APPEND _reg_entries "    { \"${_ovl}\", &${_table}, sizeof(${_table}) },\n")
            break()
        endif()
    endforeach()
    if(_table STREQUAL "")
        message(WARNING "overlays.txt: overlay ${_ovl} has no C function table yet")
    endif()
endforeach()
list(LENGTH OVERLAY_SRCS _n_ovl)
message(STATUS "rewraithed: ${_n_ovl} overlay source files")

set(_registry "/* Generated by cmake/Game.cmake from port/overlays.txt. Do not edit. */\n#include \"common.h\"\n#include \"port/overlay.h\"\n\n${_reg_decls}\nconst PortOverlay port_overlays[] = {\n${_reg_entries}    { NULL, NULL, 0 },\n};\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/overlay_registry.c" CONTENT "${_registry}" @ONLY)
list(APPEND OVERLAY_SRCS "${CMAKE_CURRENT_BINARY_DIR}/overlay_registry.c")

foreach(_entry IN LISTS _file_defines)
    string(REPLACE "|" ";" _entry "${_entry}")
    list(GET _entry 0 _file)
    list(GET _entry 1 _def)
    game_source_path(_path "${_file}")
    set_property(SOURCE "${_path}" APPEND PROPERTY COMPILE_DEFINITIONS "${_def}")
endforeach()

# --- per-file char signedness, taken from soul-re's Makefile ---------------
# Files built with -funsigned-char on PSX get /J here, so the list follows
# the decomp automatically.
file(STRINGS "${SOULRE_DIR}/Makefile" _unsigned_lines REGEX "^\\$\\(BUILD_DIR\\)/src/.*\\.c\\.o: CFLAGS \\+= -funsigned-char")
foreach(_line IN LISTS _unsigned_lines)
    string(REGEX REPLACE "^\\$\\(BUILD_DIR\\)/src/(.*\\.c)\\.o:.*" "\\1" _file "${_line}")
    game_source_path(_path "${_file}")
    if(MSVC)
        set_property(SOURCE "${_path}" APPEND PROPERTY COMPILE_OPTIONS /J)
    else()
        set_property(SOURCE "${_path}" APPEND PROPERTY COMPILE_OPTIONS -funsigned-char)
    endif()
endforeach()

# --- port sources -------------------------------------------------------------
file(GLOB_RECURSE PORT_SRCS CONFIGURE_DEPENDS "${PORT_DIR}/src/*.c" "${PORT_DIR}/src/*.cpp")

# port/src/native: plain host code (OS headers etc.) that must not see the
# game prefix, e.g. because <windows.h> has its own RECT.
file(GLOB_RECURSE PORT_NATIVE_SRCS CONFIGURE_DEPENDS "${PORT_DIR}/src/native/*.c" "${PORT_DIR}/src/native/*.cpp")

# port/shared: settings code the launcher uses as well (plain host code too)
set(PORT_SHARED_SRCS "${PORT_DIR}/shared/settings.c" "${PORT_DIR}/shared/dc_disc.c" "${PORT_DIR}/shared/proto_disc.c")
list(APPEND PORT_SRCS ${PORT_SHARED_SRCS})
list(APPEND PORT_NATIVE_SRCS ${PORT_SHARED_SRCS})

set_source_files_properties(${PORT_NATIVE_SRCS} PROPERTIES COMPILE_DEFINITIONS PORT_NATIVE)
set_property(SOURCE "${PORT_DIR}/src/native/discord_rpc.c" APPEND PROPERTY COMPILE_DEFINITIONS
    "REWRAITHED_DISCORD_APP_ID=\"${REWRAITHED_DISCORD_APP_ID}\""
    "REWRAITHED_VERSION_TEXT=\"${REWRAITHED_VERSION}\"")

include("${CMAKE_CURRENT_LIST_DIR}/Hasm.cmake")

add_executable(rewraithed ${GAME_SRCS} ${OVERLAY_SRCS} ${PORT_SRCS} "${HASM_OUTPUT}" ${LIBGTE_OUTPUT} ${GAME_ASM_OUTPUTS})
target_compile_definitions(rewraithed PRIVATE PORT_GAME_LIBGTE=${PORT_GAME_LIBGTE})

# The game looks for the disc image in a "disc" folder next to the exe
# (port/src/native/disc_folder_win32.c); create it so it is easy to find.
add_custom_command(TARGET rewraithed POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:rewraithed>/disc"
    VERBATIM
)

target_include_directories(rewraithed PRIVATE
    "${PORT_DIR}/include"
    "${PORT_DIR}/shared"
    "${SOULRE_DIR}/include"
    "${GAME_FIXED_DIR}"   # patched copies shadow soul-re/src for #include "Game/..."
    "${SOULRE_DIR}/src"
)

target_compile_definitions(rewraithed PRIVATE
    PC_PORT
    NON_MATCHING   # use the decomp's C bodies for not-yet-matching functions
    # SKIP_ASM stays undefined: overlays keep their real build in the
    # "#ifndef SKIP_ASM" branch. INCLUDE_ASM still compiles to nothing,
    # because the prefix turns __asm__ into an empty macro.
    USE_PSYQ       # PsyQ-style headers, provided by PsyCross
    _LANGUAGE_C
    SDL_MAIN_HANDLED
)

if(MSVC)
    target_compile_options(rewraithed PRIVATE
        "/FI${PORT_DIR}/include/port/prefix.h"
        /W1
        /wd4068  # unknown pragma (GCC pragmas in soul-re headers)
        /wd4005  # macro redefinition (soul-re SDK.h vs PsyCross headers)
        /wd4244 /wd4996 /wd4018 /wd4146 /wd4716 /wd4033 /wd4715
    )
    # Ordering-table links are 24-bit (see port/memory_map.h): the game's
    # static data must stay below 16 MB, so the image keeps a fixed low base,
    # and the PSX-style RAM view at 0x80000000 needs a large-address-aware
    # process.
    target_link_options(rewraithed PRIVATE /DYNAMICBASE:NO /FIXED /BASE:0x400000 /LARGEADDRESSAWARE)
    set_target_properties(rewraithed PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    target_compile_options(rewraithed PRIVATE -include "${PORT_DIR}/include/port/prefix.h")
endif()

target_link_libraries(rewraithed PRIVATE psycross SDL2::SDL2-static)
