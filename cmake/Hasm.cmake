# Hand-written MIPS from soul-re (HASM_1.s, HASM_2.s), recompiled to C at
# build time by tools/hasm2c.py. Regenerated whenever soul-re's assembly or
# the tool changes, so it follows the decomp automatically.
#
# Routines with a hand-written C version in port/src/hasm are not listed here.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# C-callable routines: NAME:ARGS[:ret]
set(HASM_ENTRIES
    # maths and collision
    MATH3D_racos_S:1:ret
    RotMatrixZYX:2:ret
    # libgte routines the game carries in its own assembly. PsyCross has its
    # own versions under the same names (and ScaleMatrix scales rows where
    # PsyQ scales columns), so these are exported as port_<name> and the game
    # is routed to them by port/include/port/libgte_game.h.
    RotMatrix:2:ret:port_RotMatrix
    RotMatrixX:2:ret:port_RotMatrixX
    RotMatrixY:2:ret:port_RotMatrixY
    RotMatrixZ:2:ret:port_RotMatrixZ
    ScaleMatrix:2:ret:port_ScaleMatrix
    rsin:1:ret:port_rsin
    rcos:1:ret:port_rcos
    G2Quat_ToMatrix_S:2
    G2Quat_FromMatrix_S:2
    G2Quat_FromEuler_S:2
    COLLIDE_IntersectLineAndPlane_S:5:ret
    COLLIDE_NearestPointOnLine_S:4:ret
    COLLIDE_NearestPointOnPlane_S:4
    # rendering
    DRAW_DisplayPolytopeList_S:6:ret
    BSP_MarkVisibleLeaves_S:3
    DRAW_AnimatedModel_S:5:ret
    PIPE3D_TransformAnimatedInstanceVertices_S:8:ret
    DRAW_SplitModel_S:6:ret
    VM_ProcessVMObjectList_S:2
    VM_ProcessVMObjectSetColor_S:2
)

# Routines only called from other hand-written routines.
set(HASM_INTERNAL
    DRAW_DisplaySubdivPolytope_S
    DRAW_DisplayTFace_S
    DRAW_MFace3_S
    BSP_SphereIntersectsViewVolume_S
    func_80078458
)

# C functions the assembly calls: NAME:ARGS
set(HASM_EXTERNS
    DRAW_Zclip_subdiv:3
    MATH3D_FastSqrt:1
)

set(HASM_SOURCES "${SOULRE_DIR}/src/Game/HASM_1.s" "${SOULRE_DIR}/src/Game/HASM_2.s")
set(HASM_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/hasm_recomp.c")

set(_hasm_args --gte "${SOULRE_DIR}/include/gte_macros.s" --out "${HASM_OUTPUT}")
foreach(_e IN LISTS HASM_ENTRIES)
    list(APPEND _hasm_args --entry ${_e})
endforeach()
foreach(_f IN LISTS HASM_INTERNAL)
    list(APPEND _hasm_args --func ${_f})
endforeach()
foreach(_x IN LISTS HASM_EXTERNS)
    list(APPEND _hasm_args --extern ${_x})
endforeach()

add_custom_command(
    OUTPUT "${HASM_OUTPUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/hasm2c.py" ${_hasm_args} ${HASM_SOURCES}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/hasm2c.py" ${HASM_SOURCES} "${SOULRE_DIR}/include/gte_macros.s"
    COMMENT "Recompiling soul-re hand-written MIPS to C"
    VERBATIM
)

# Standalone C: no game prefix.
set_source_files_properties("${HASM_OUTPUT}" PROPERTIES COMPILE_DEFINITIONS PORT_NATIVE GENERATED TRUE)

# --- original compiled functions (port/asm_functions.txt, see cmake/Game.cmake)
set(GAME_ASM_OUTPUTS "")
foreach(_spec IN LISTS GAME_ASM_FUNCTIONS)
    string(REPLACE "|" ";" _spec "${_spec}")
    list(GET _spec 0 _func)
    list(GET _spec 1 _asm)
    # remaining items: the asm_functions.txt line (file, entry, callees...)
    list(SUBLIST _spec 3 -1 _rest)
    list(GET _rest 0 _entry)
    list(SUBLIST _rest 1 -1 _callees)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/generated/asmfn_${_func}.c")
    set(_args --gte "${SOULRE_DIR}/include/gte_macros.s" --out "${_out}" --entry "${_entry}")
    foreach(_c IN LISTS _callees)
        list(APPEND _args --extern "${_c}")
    endforeach()
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/hasm2c.py" ${_args} "${_asm}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/hasm2c.py" "${_asm}" "${SOULRE_DIR}/include/gte_macros.s"
        COMMENT "Recompiling the original ${_func}"
        VERBATIM
    )
    set_source_files_properties("${_out}" PROPERTIES COMPILE_DEFINITIONS PORT_NATIVE GENERATED TRUE)
    list(APPEND GAME_ASM_OUTPUTS "${_out}")
endforeach()

# --- the PsyQ libgte routines linked into the executable ----------------------
# soul-re's splat output (asm/lib/libgte, not part of its git tree) holds the
# original library code. When present, the pure GTE routines are recompiled
# from it and the game uses them (as port_<name>, port/include/port/libgte_game.h)
# instead of PsyCross's reimplementations. Routines that need the library's
# own data (PushMatrix/PopMatrix, InitGeom, SetFogNearFar, ratan2) stay with
# PsyCross.
set(LIBGTE_ASM_DIR "${SOULRE_DIR}/asm/lib/libgte")
set(LIBGTE_ENTRIES
    TransposeMatrix:2:ret
    LoadAverageShort12:5
    LoadAverageCol:5
    MulMatrix0:3:ret
    MulMatrix2:2:ret
    CompMatrix:3:ret
    ApplyMatrix:3:ret
    ApplyMatrixSV:3:ret
    ApplyMatrixLV:3:ret
    TransMatrix:2:ret
    RotTrans:3:ret
    SetRotMatrix:1
    SetLightMatrix:1
    SetColorMatrix:1
    SetTransMatrix:1
    SetDQA:1
    SetDQB:1
    SetBackColor:3
    SetFarColor:3
    SetGeomOffset:2
    SetGeomScreen:1
)

set(LIBGTE_OUTPUT "")
file(GLOB LIBGTE_SOURCES "${LIBGTE_ASM_DIR}/*.s")
if(LIBGTE_SOURCES)
    set(LIBGTE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/libgte_recomp.c")
    set(_libgte_args --gte "${SOULRE_DIR}/include/gte_macros.s" --out "${LIBGTE_OUTPUT}")
    foreach(_e IN LISTS LIBGTE_ENTRIES)
        string(REGEX REPLACE ":.*" "" _name "${_e}")
        if(_e MATCHES ":ret$")
            list(APPEND _libgte_args --entry "${_e}:port_${_name}")
        else()
            list(APPEND _libgte_args --entry "${_e}:void:port_${_name}")
        endif()
    endforeach()

    add_custom_command(
        OUTPUT "${LIBGTE_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/hasm2c.py" ${_libgte_args} ${LIBGTE_SOURCES}
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/hasm2c.py" ${LIBGTE_SOURCES} "${SOULRE_DIR}/include/gte_macros.s"
        COMMENT "Recompiling the game's PsyQ libgte routines to C"
        VERBATIM
    )
    set_source_files_properties("${LIBGTE_OUTPUT}" PROPERTIES COMPILE_DEFINITIONS PORT_NATIVE GENERATED TRUE)
    set(PORT_GAME_LIBGTE 1)
    message(STATUS "rewraithed: using the game's own libgte routines from ${LIBGTE_ASM_DIR}")
else()
    set(PORT_GAME_LIBGTE 0)
    message(WARNING "rewraithed: ${LIBGTE_ASM_DIR} not found (run splat in soul-re); using PsyCross's libgte, which differs from PsyQ in places")
endif()
