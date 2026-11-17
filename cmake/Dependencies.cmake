# Third-party libraries fetched at configure time and linked statically.
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# --- SDL2 -------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# --- OpenAL Soft ------------------------------------------------------------
set(LIBTYPE STATIC CACHE STRING "" FORCE)
set(ALSOFT_UTILS        OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS        OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL      OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG    OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(openal
    URL https://github.com/kcat/openal-soft/archive/refs/tags/1.24.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SDL2 openal)
