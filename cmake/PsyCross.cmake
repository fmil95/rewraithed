# PsyCross: PC implementation of the PsyQ libraries (GTE, GPU, SPU, CD, pad).
# Built from the submodule with our own target so its options stay pinned
# here. Files patched through psycross_fixup (port/fixups.cmake) are compiled
# from build/fixed-psycross instead; must be included after Game.cmake.

file(GLOB_RECURSE PSYCROSS_SRCS_C   CONFIGURE_DEPENDS "${PSYCROSS_DIR}/src/*.c")
file(GLOB_RECURSE PSYCROSS_SRCS_CPP CONFIGURE_DEPENDS "${PSYCROSS_DIR}/src/*.cpp")

foreach(_file IN LISTS _psycross_patched)
    list(TRANSFORM PSYCROSS_SRCS_C   REPLACE "^${PSYCROSS_DIR}/src/${_file}$" "${PSYCROSS_FIXED_DIR}/${_file}")
    list(TRANSFORM PSYCROSS_SRCS_CPP REPLACE "^${PSYCROSS_DIR}/src/${_file}$" "${PSYCROSS_FIXED_DIR}/${_file}")
endforeach()

set_source_files_properties(${PSYCROSS_SRCS_C}   PROPERTIES LANGUAGE C)
set_source_files_properties(${PSYCROSS_SRCS_CPP} PROPERTIES LANGUAGE CXX)

# port/psycross: the port's additions to the renderer (view rectangle for
# pillarboxing, MSAA-safe framebuffer readback, 2D picture filtering), called
# from hooks the fixups insert.
file(GLOB PSYCROSS_PORT_SRCS CONFIGURE_DEPENDS "${PORT_DIR}/psycross/*.cpp")

add_library(psycross STATIC ${PSYCROSS_SRCS_C} ${PSYCROSS_SRCS_CPP} ${PSYCROSS_PORT_SRCS})

target_include_directories(psycross PUBLIC
    "${PSYCROSS_DIR}/include"
    "${PSYCROSS_DIR}/include/psx"
)

# Patched copies still include their neighbours relatively ("../platform.h",
# "PsyX_GPU.h"); these let such includes resolve to the submodule's files.
file(GLOB _psycross_src_dirs LIST_DIRECTORIES true "${PSYCROSS_DIR}/src/*")
list(FILTER _psycross_src_dirs INCLUDE REGEX "/[^./]+$")
target_include_directories(psycross PRIVATE "${PSYCROSS_DIR}/src" ${_psycross_src_dirs})

# These change primitive and OT layouts, so they are PUBLIC: the game must be
# compiled with exactly the same values. Soul Reaver builds its own primitives
# with the classic single-word tag, so extended pointers (and PGXP) stay off.
target_compile_definitions(psycross PUBLIC
    USE_EXTENDED_PRIM_POINTERS=0
    USE_PGXP=0
    _CRT_SECURE_NO_WARNINGS
    $<$<PLATFORM_ID:Windows>:_WINDOWS>
)

if(MSVC)
    target_compile_options(psycross PRIVATE /W1 /wd4244 /wd4996)
endif()

target_link_libraries(psycross PUBLIC SDL2::SDL2-static OpenAL::OpenAL)
