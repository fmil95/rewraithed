cmake_minimum_required(VERSION 3.24)

project(rewraithed LANGUAGES C CXX)

# Shown by the launcher; bump with each release (v0.99p is Beta 16).
set(REWRAITHED_VERSION "0.99p")

# Discord Rich Presence: the Application ID of the "Rewraithed" application
# in the Discord Developer Portal (discord.com/developers/applications).
# Empty leaves Rich Presence out.
set(REWRAITHED_DISCORD_APP_ID "1573080556147576943")

# The decompiled game stores pointers in 32-bit fields and links GPU primitives
# through 24-bit addresses, so the port is 32-bit only for now.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "rewraithed must be built as a 32-bit target (e.g. -A Win32).")
endif()

set(SOULRE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/soul-re")
set(PSYCROSS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PsyCross")
set(PORT_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/port")

if(NOT EXISTS "${SOULRE_DIR}/src/Game")
    message(FATAL_ERROR "soul-re submodule missing: run 'git submodule update --init'.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MSVC run-time checks (/RTC1) stop on reads of uninitialised locals, which
# the decompiled PSX code does in places where the value is never used.
if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

include(cmake/Dependencies.cmake)
include(cmake/Game.cmake)       # also prepares the patched PsyCross files
include(cmake/PsyCross.cmake)
include(cmake/Launcher.cmake)
