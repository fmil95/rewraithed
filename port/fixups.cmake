# Source fixups: literal text replacements applied to copies of soul-re files
# at configure time (soul-re itself is never modified).
#
# Only for these cases:
#   - constructs the PC compiler cannot accept at all (GNU C extensions);
#   - hard-coded PSX memory-map constants inside functions that cannot be
#     replaced through overrides.txt (their file-static state is not
#     reachable from port/src);
#   - reads through NULL that are harmless on PSX (address 0 is RAM holding
#     zeros) but fault on PC;
#   - one-line hooks for the PC options of the launcher (FPS limit, preload,
#     renderer settings), each calling into port/src or port/psycross.
# Compiler fixups are candidates for matching-neutral upstream changes in
# soul-re; once upstream no longer contains FIND, configure warns and the
# entry can be deleted.
#
#   game_fixup(<file under soul-re/src>  FIND <text>  REPLACE <text>)

# GNU C: arithmetic on void* (byte offset).
game_fixup(Game/PHYSOBS.c
    FIND    "(BreakOffData *)(instance->extraData + 4)"
    REPLACE "(BreakOffData *)((char *)instance->extraData + 4)")

game_fixup(Game/PSX/MAIN.c
    FIND    "gameTrackerX.reqDisp = gameTrackerX.disp + ("
    REPLACE "gameTrackerX.reqDisp = (char *)gameTrackerX.disp + (")

# GNU C / C23: case label at the end of a compound statement.
game_fixup(Game/PSX/MAIN.c
    FIND    "            case 5:\n            }"
    REPLACE "            case 5:\n                break;\n            }")

# Memory map: the heap runs from overlayAddress to the end of the 2 MB of PSX
# RAM. On PC overlayAddress points at a static buffer (port/src/memory.c).
game_fixup(Game/MEMPACK.c
    FIND    "newMemTracker.totalMemory = (0x80000000 + (0x200000 - (0x100000 / 256))) - (intptr_t)overlayAddress;"
    REPLACE "newMemTracker.totalMemory = port_heap_size;")

# FPS limit (launcher setting): other than at the PSX's 30, the measured
# frame time may go under 33 ms, a plane shift keeps the measured time
# too, and the two-vblank display wait gives way to port_frame_limit().
game_fixup(Game/GAMELOOP.c
    FIND    "            if ((last < (unsigned int)lockRate) || (gameTrackerX.gameData.asmData.MorphTime != 1000))"
    REPLACE "            if ((last < port_min_frame_ms(lockRate)) || ((gameTrackerX.gameData.asmData.MorphTime != 1000) && port_fps_locked()))")

game_fixup(Game/GAMELOOP.c
    FIND    "    if (gameTracker->vblFrames <= (unsigned long)gameTracker->frameRateLock)\n    {\n        while (CheckVolatile(gameTracker->reqDisp) != 0)"
    REPLACE "    port_frame_limit();\n\n    if (port_fps_locked() && (gameTracker->vblFrames <= (unsigned long)gameTracker->frameRateLock))\n    {\n        while (CheckVolatile(gameTracker->reqDisp) != 0)")

# Preload Chunks (launcher setting): the units next to the one entered are
# loaded right away instead of in the background. This waits once the move
# into the new unit is complete: waiting inside it (where the loads are
# queued) finished loads before the unit was set up, and the object
# cleanup that follows met trackers not yet filled in (seen at roninbss).
game_fixup(Game/STREAM.c
    FIND    "    gameTrackerX.SwitchToNewStreamUnit = 0;\n\n    if (gameTrackerX.SwitchToNewWarpIndex != -1)\n    {\n        SndPlayVolPan(388, 127, 64, 0);\n\n        CurrentWarpNumber = gameTrackerX.SwitchToNewWarpIndex;\n    }\n}\n"
    REPLACE "    gameTrackerX.SwitchToNewStreamUnit = 0;\n\n    if (gameTrackerX.SwitchToNewWarpIndex != -1)\n    {\n        SndPlayVolPan(388, 127, 64, 0);\n\n        CurrentWarpNumber = gameTrackerX.SwitchToNewWarpIndex;\n    }\n\n    port_preload_wait();\n}\n")

# NULL read: blinking menu items (flashStart) pass NULL text on "off" frames.
game_fixup(Game/MENU/MENU.c
    FIND    "    texLen = strlen(text);"
    REPLACE "    if (text == NULL) text = \"\";\n\n    texLen = strlen(text);")

# NULL read: Zephon's back half picks its autoface point from its segment
# matrices already in its init, before the instance has any.
game_fixup(Overlays/walbosb/walbosb.c
    FIND    "                    MON_SphereWorldPos(&instance->matrix[hPrim->segment], hSphere, &pos);"
    REPLACE "                    if (instance->matrix == NULL) break;\n\n                    MON_SphereWorldPos(&instance->matrix[hPrim->segment], hSphere, &pos);")

# NULL read: Rahab's damage effect copies the hit location before checking
# that there is a hit.
game_fixup(Overlays/alukabss/alukabss.c
    FIND    "    localloc = data->location;\n"
    REPLACE "    if (data != NULL)\n    {\n        localloc = data->location;\n    }\n")

# NULL read: a monster that dies without a hit message (seen while Raziel
# swims in city19) is knocked back with no hit data; on PSX that reads
# zeros, i.e. no knockback.
game_fixup(Game/MONSTER/MONLIB.c
    FIND    "    d = data->knockBackDistance;\n    t = data->knockBackDuration;\n"
    REPLACE "    d = (data != NULL) ? data->knockBackDistance : 0;\n    t = (data != NULL) ? data->knockBackDuration : 0;\n")

# NULL read: a monster first sensing an ally instance that has no extra
# data (seen on the water path between city19 and city9); on PSX the flag
# test reads zero there.
game_fixup(Game/MONSTER/MONSENSE.c
    FIND    "            if (((Object *)sensed->extraData)->oflags & 0x100000)"
    REPLACE "            if ((sensed->extraData != NULL) && (((Object *)sensed->extraData)->oflags & 0x100000))")

# NULL read: with the autoface camera (R1) on, a portal whose unit is not
# loaded has no toStreamUnit (seen among Dumahim); on PSX the unit ID read
# there does not match, so nothing is drawn through it.
game_fixup(Game/GAMELOOP.c
    FIND    "                    streamID = streamPortal2->toStreamUnit->StreamUnitID;\n"
    REPLACE "                    if (streamPortal2->toStreamUnit == NULL)\n                    {\n                        continue;\n                    }\n\n                    streamID = streamPortal2->toStreamUnit->StreamUnitID;\n")

# NULL read: a monster heading back to its post or last valid position
# (e.g. after being thrown into water by the Force Glyph) has no target,
# but the pursue code still reads its distance; on PSX that reads zero.
game_fixup(Game/MONSTER/MONSTER.c
    FIND    "        if (target->distance < (range + MON_GetAnim(instance, mv->subAttr->animList, 2)->distance)) "
    REPLACE "        if (((target != NULL) ? target->distance : 0) < (range + MON_GetAnim(instance, mv->subAttr->animList, 2)->distance)) ")

# Spectral death effect: the displayed page is copied and drawn over itself
# while the underworld loads. The port puts the frame being shown into VRAM
# first and keeps it in the window between steps (port/src/gpu_hooks.c).
game_fixup(Game/UNDRWRLD.c
    FIND    "    rect.h = 240;\n\n    MoveImage(&rect, 0, (UNDRWRLD_GetDispPage() ^ 1) << 8);"
    REPLACE "    rect.h = 240;\n\n    port_store_shown_frame(rect.x, rect.y, rect.w, rect.h);\n    port_draw_over_shown(1);\n\n    MoveImage(&rect, 0, (UNDRWRLD_GetDispPage() ^ 1) << 8);")

# The effect runs only while the underworld loads, which takes far less
# time than from the PSX's CD: it keeps going for a minimum time.
game_fixup(Game/UNDRWRLD.c
    FIND    "    streamUnit = STREAM_LoadLevel(baseAreaName, NULL, 0);\n\n    while (STREAM_PollLoadQueue() != 0)\n    {\n        UW_time = UNDERWORLD_RotateScreenStep(UW_time);\n    }\n"
    REPLACE "    streamUnit = STREAM_LoadLevel(baseAreaName, NULL, 0);\n\n    while ((STREAM_PollLoadQueue() != 0) || port_draw_over_shown_hold())\n    {\n        UW_time = UNDERWORLD_RotateScreenStep(UW_time);\n    }\n")

game_fixup(Game/UNDRWRLD.c
    FIND    "    UNDERWORLD_LoadLevel(\"under1\", &gameTrackerX);\n"
    REPLACE "    UNDERWORLD_LoadLevel(\"under1\", &gameTrackerX);\n\n    port_draw_over_shown(0);\n")

# Interrupt handlers run on the port's vblank thread, at the same time as
# the game, while on PSX they interrupt it. The game clears drawTimerReturn
# and reqDisp outside critical sections, so a handler could see a pointer
# set and then use it after the game cleared it (seen in a cathy area).
# The handlers read each pointer once.
game_fixup(Game/PSX/MAIN.c
    FIND    "    if (gameTrackerX.drawTimerReturn != NULL)\n    {\n        *gameTrackerX.drawTimerReturn = TIMER_TimeDiff(gameTrackerX.usecsStartDraw);\n"
    REPLACE "    long *drawTimerReturn = gameTrackerX.drawTimerReturn;\n\n    if (drawTimerReturn != NULL)\n    {\n        *drawTimerReturn = TIMER_TimeDiff(gameTrackerX.usecsStartDraw);\n")

game_fixup(Game/PSX/MAIN.c
    FIND    "    if ((gameTrackerX.reqDisp != NULL) && (gameTrackerX.vblFrames > (unsigned long)gameTrackerX.frameRateLock))\n    {\n        PutDispEnv(gameTrackerX.reqDisp);\n"
    REPLACE "    void *reqDisp = gameTrackerX.reqDisp;\n\n    if ((reqDisp != NULL) && (gameTrackerX.vblFrames > (unsigned long)gameTrackerX.frameRateLock))\n    {\n        PutDispEnv(reqDisp);\n")

# Glyph menu: glyphs on the left half of the ring were sized from an
# uninitialised local; the original loads 4096 in both branches (on PC the
# stack garbage made them too small, e.g. the Force glyph).
game_fixup(Game/GLYPH.c
    FIND    "            scale_modify = temp + (AngleDiff(rot, 3072) << 1); // bug in the original code?"
    REPLACE "            temp = 4096;\n\n            scale_modify = temp + (AngleDiff(rot, 3072) << 1);")

# Texels past a polygon's bottom edge: at scales such as 4.5x (240 lines
# shown 1080 high) some polygon edges fall exactly on a row of pixel
# centres. OpenGL draws such a row on the bottom edge (in window
# coordinates its lower edge), where the texture coordinate equals the
# exclusive end the PSX never samples: a thin line from the next texture
# row appeared under letters of the menus. Pixels are sampled a hundredth
# of a pixel up, inside the polygon, which only changes those on an edge.
psycross_fixup(render/PsyX_render.cpp
    FIND    "\t\"\t\tvec2 rg = samplePSX(P);\\n\"\\\n"
    REPLACE "\t\"\t\tvec2 rg = samplePSX(P + 0.01 * dFdy(P));\\n\"\\\n")

# NULL read: a damaged monster's blood and spark effects read a segment
# position from its matrices, which it may not have that frame (the Tomb
# Guardian while Raziel shifted back to the material realm); on PSX that
# reads low RAM. Skip the effects without matrices.
game_fixup(Game/MONSTER/MONSTER.c
    FIND    "    mv = (MonsterVars *)instance->extraData;\n\n    if (data == NULL)\n    {\n        MonsterAttributes *ma;\n"
    REPLACE "    mv = (MonsterVars *)instance->extraData;\n\n    if ((data == NULL) && (instance->matrix == NULL))\n    {\n        return;\n    }\n\n    if (data == NULL)\n    {\n        MonsterAttributes *ma;\n")

# Warp gate destinations: on the PSX (checked on DuckStation) Right steps
# forward through the destinations and Left back. The decompiled handler,
# like the disassembly, calls IncrementIndex on pad bit 0x4 (Left in the
# port's input, which walks Raziel left correctly), so the port went the
# other way. Where the original's input differs has not been found; the
# calls are swapped to match the console.
game_fixup(Game/RAZIEL/PUPPET.c
    FIND    "                if ((*PadData & 0x4))\n                {\n                    WARPGATE_IncrementIndex();\n                }\n\n                if ((*PadData & 0x8))\n                {\n                    WARPGATE_DecrementIndex();\n                }"
    REPLACE "                if ((*PadData & 0x4))\n                {\n                    WARPGATE_DecrementIndex();\n                }\n\n                if ((*PadData & 0x8))\n                {\n                    WARPGATE_IncrementIndex();\n                }")

# Debug menu "MUSIC ON": the prototype moved it to the main menu but only
# its (now empty) sound menu ever applied it, so it did nothing. Apply it
# when the debug menu (game mode 4, not the pause menu) closes.
game_fixup(Game/DEBUG.c
    FIND    "    currentMenu[DEBUG_MenuCountLength(currentMenu)].lower = debugMenuChoice;\n\n    if (gameTrackerX.sound.gMusicOn == 0)\n"
    REPLACE "    currentMenu[DEBUG_MenuCountLength(currentMenu)].lower = debugMenuChoice;\n\n    if (gameTrackerX.gameMode == 4)\n    {\n        gameTrackerX.sound.gMusicOn = (gameTrackerX.debugFlags2 & 0x1000) != 0;\n    }\n\n    if (gameTrackerX.sound.gMusicOn == 0)\n")

# --- PsyCross ----------------------------------------------------------------
# psycross_fixup(<file under PsyCross/src> FIND <text> REPLACE <text>)
# Behaviour PsyCross does not reproduce from the PSX hardware.

# VRAM copies (MoveImage, DR_MOVE packets): the GPU wraps coordinates and
# never writes outside its 1024x512 VRAM; clip instead of overrunning memory.
psycross_fixup(render/PsyX_render.cpp
    FIND    "void GR_CopyVRAM(unsigned short* src, int x, int y, int w, int h, int dst_x, int dst_y)\n{\n\tvram_need_update = 1;\n"
    REPLACE "void GR_CopyVRAM(unsigned short* src, int x, int y, int w, int h, int dst_x, int dst_y)\n{\n\tvram_need_update = 1;\n\n\tdst_x &= VRAM_WIDTH - 1;\n\tdst_y &= VRAM_HEIGHT - 1;\n\tif (!src)\n\t{\n\t\tx &= VRAM_WIDTH - 1;\n\t\ty &= VRAM_HEIGHT - 1;\n\t\tif (x + w > VRAM_WIDTH) w = VRAM_WIDTH - x;\n\t\tif (y + h > VRAM_HEIGHT) h = VRAM_HEIGHT - y;\n\t}\n\tif (dst_x + w > VRAM_WIDTH) w = VRAM_WIDTH - dst_x;\n\tif (dst_y + h > VRAM_HEIGHT) h = VRAM_HEIGHT - dst_y;\n\tif (w <= 0 || h <= 0)\n\t\treturn;\n")

# Frame readback at the display area: the debug menu's VIEW VRAM moves the
# display around VRAM and lets it go 32 pixels past either edge, where the
# readback asserts. Skip the readback while the area is not inside VRAM.
psycross_fixup(render/PsyX_render.cpp
    FIND    "void GR_StoreFrameBuffer(int x, int y, int w, int h)\n{\n#if USE_OPENGL\n"
    REPLACE "void GR_StoreFrameBuffer(int x, int y, int w, int h)\n{\n\tif (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > VRAM_WIDTH || y + h > VRAM_HEIGHT)\n\t\treturn;\n\n#if USE_OPENGL\n")

# GPU packets: the hardware reads the tag, then `len` command words back to
# back, each sized by its own opcode. PsyCross assumes one tag per command,
# but Soul Reaver packs several commands per packet (e.g. a texture-page word
# before a polygon, or two POLY_G4 in one packet). Walk the packet as a
# command stream instead; each command is handed to ParsePrimitive through a
# "tag" pointer one word before it.
psycross_fixup(gpu/PsyX_GPU.cpp
    FIND    "\t\t\t\twhile (currentPacket < endPacket)\n\t\t\t\t{\n\t\t\t\t\tprimLength = ParsePrimitive(reinterpret_cast<P_TAG*>(currentPacket));\n\t\t\t\t\tcurrentPacket += (primLength + P_LEN) * sizeof(u_int);\n\t\t\t\t}\n"
    REPLACE "\t\t\t\twhile (currentPacket + P_LEN * sizeof(u_int) < endPacket)\n\t\t\t\t{\n\t\t\t\t\tprimLength = ParsePrimitive(reinterpret_cast<P_TAG*>(currentPacket));\n\t\t\t\t\tif (primLength <= 0)\n\t\t\t\t\t\tprimLength = 1;\n\t\t\t\t\tcurrentPacket += primLength * sizeof(u_int);\n\t\t\t\t}\n\t\t\t\tcurrentPacket += P_LEN * sizeof(u_int);\n")

# Ordering-table links are 24-bit: decode them like the PSX (port/memory_map.h).
psycross_fixup(gpu/PsyX_GPU.cpp
    FIND    "basePacket = reinterpret_cast<uintptr_t>(nextPrim(basePacket))"
    REPLACE "basePacket = ((reinterpret_cast<P_TAG*>(basePacket)->addr < 0x400000u) ? (reinterpret_cast<P_TAG*>(basePacket)->addr | 0x80000000u) : reinterpret_cast<P_TAG*>(basePacket)->addr)")

# ...which makes each E1-E6 environment word its own one-word command.
psycross_fixup(gpu/PsyX_GPU.cpp
    FIND    "\tfor (int i = 0; i < polyTag->len; ++i)\n\t{\n\t\tconst u_int code = codePtr[i];"
    REPLACE "\tfor (int i = 0; i < 1; ++i)\n\t{\n\t\tconst u_int code = codePtr[i];")

# PsyCross renders off-screen (into a texture) whenever the current draw
# environment has dfe = 0. On PSX that bit only says whether drawing may
# touch the displayed area; Soul Reaver clears it in some texture-page
# commands (e.g. the fade-out quad), which here sent the rest of the frame
# off-screen. Keep dfe as set by PutDrawEnv; ignore it in E1 commands.
psycross_fixup(gpu/PsyX_GPU.cpp
    FIND    "\t\t\tactiveDrawEnv.dfe = (code >> 10) & 1;\n"
    REPLACE "\t\t\t/* dfe bit ignored: see rewraithed port/fixups.cmake */\n")

# Opcodes 0x00-0x02 by the hardware's sizes: 0x00 NOP and 0x01 cache flush
# are one word (a DR_MOVE is a flush followed by an 0x80 copy command),
# 0x02 is a 3-word VRAM fill. 0x80 is the VRAM-to-VRAM copy.
psycross_fixup(gpu/PsyX_GPU.cpp
    FIND    "\t\tconst int primSubType = polyTag->code & 0x0F;\n\t\tif (primSubType == 0x0)\n\t\t{\n\t\t\tprimLength = 3;\n\t\t}\n\t\telse if (primSubType == 0x1)\n\t\t{\n\t\t\tDR_MOVE* drmove = (DR_MOVE*)polyTag;\n\n\t\t\tconst int y = drmove->code[3] >> 0x10 & 0xFFFF;\n\t\t\tconst int x = drmove->code[3] & 0xFFFF;\n\n\t\t\tRECT16 rect;\n\t\t\t*(uint*)&rect.x = *(uint*)&drmove->code[2];\n\t\t\t*(uint*)&rect.w = *(uint*)&drmove->code[4];\n\n\t\t\tMoveImage(&rect, x, y);\n\t\t\tprimLength = 5;\n\t\t}\n\t\tbreak;\n\t}"
    REPLACE "\t\tconst int primSubType = polyTag->code & 0x0F;\n\t\tconst u_int* cmd = (const u_int*)polyTag + P_LEN;\n\t\tif (primSubType == 0x2)\n\t\t{\n\t\t\tRECT16 rect;\n\t\t\t*(uint*)&rect.x = cmd[1];\n\t\t\t*(uint*)&rect.w = cmd[2];\n\t\t\tClearImage(&rect, cmd[0] & 0xFF, (cmd[0] >> 8) & 0xFF, (cmd[0] >> 16) & 0xFF);\n\t\t\tprimLength = 3;\n\t\t}\n\t\telse\n\t\t{\n\t\t\tprimLength = 1;\n\t\t}\n\t\tbreak;\n\t}\n\tcase 0x80:\n\t{\n\t\tconst u_int* cmd = (const u_int*)polyTag + P_LEN;\n\t\tRECT16 rect;\n\t\t*(uint*)&rect.x = cmd[1];\n\t\t*(uint*)&rect.w = cmd[3];\n\t\tMoveImage(&rect, cmd[2] & 0xFFFF, cmd[2] >> 16);\n\t\tprimLength = 4;\n\t\tbreak;\n\t}")

# Renderer options (launcher settings), implemented in port/psycross:
# a view rectangle instead of the whole window (pillarboxing), readback
# from it that also works with a multisampled window, the texture filter
# overridable per draw, and dithering that can be switched off.
psycross_fixup(render/PsyX_render.cpp
    FIND    "int g_cfg_bilinearFiltering = 0;\n"
    REPLACE "int g_cfg_bilinearFiltering = 0;\nextern \"C\" {\nextern int g_cfg_dithering;\nvoid PsyX_PortSetView(void);\nvoid PsyX_PortScissor(float x, float y, float w, float h);\nvoid PsyX_PortClear(void);\nvoid PsyX_PortBeforeSwap(void);\nvoid PsyX_PortBlitFromWindow(int x, int y, int w, int h);\nint PsyX_PortTextureFilter(void);\n}\n")

# Frame readback into VRAM (pictures the game renders and reuses as
# textures, e.g. a warp gate's view of its destination): the pixels come
# back as RGBA bytes, but the conversion took the first byte as blue, so
# red and blue were swapped (the gate showed orange).
psycross_fixup(render/PsyX_render.cpp
    FIND    "\t\t\tu_char b = ((c >> 3) & 0x1F);\n\t\t\tu_char g = ((c >> 11) & 0x1F);\n\t\t\tu_char r = ((c >> 19) & 0x1F);"
    REPLACE "\t\t\tu_char r = ((c >> 3) & 0x1F);\n\t\t\tu_char g = ((c >> 11) & 0x1F);\n\t\t\tu_char b = ((c >> 19) & 0x1F);")

# Blend mode 3 (background + 1/4 foreground) is drawn with the constant
# alpha as the source factor, but switching blending on set it to 0.5, so
# such polygons were twice as bright (e.g. a dying Sluagh).
psycross_fixup(render/PsyX_render.cpp
    FIND    "\t\t\tglBlendColor(0.25f, 0.25f, 0.25f, 0.5f);"
    REPLACE "\t\t\tglBlendColor(0.25f, 0.25f, 0.25f, 0.25f);")

# Dithering in PSX pixels: PsyCross applied the 4x4 dither pattern per
# window pixel. Pictures the game renders and reuses at PSX resolution (the
# warp gate's view of its destination) then sampled that fine pattern into
# a coarse checker. The pattern is now scaled to PSX pixels (ditherScale:
# window pixels per PSX pixel when drawing on screen, 1 off screen), as on
# the console and like DuckStation's default "scaled dithering".
psycross_fixup(render/PsyX_render.cpp
    FIND    "\t\t\"\t\tivec2 dc = ivec2(fract(gl_FragCoord.xy / 4.0) * 4.0);\\n\"\\\n"
    REPLACE "\t\t\"\t\tivec2 dc = ivec2(mod(floor(gl_FragCoord.xy / max(ditherScale, 1.0)), 4.0));\\n\"\\\n")

psycross_fixup(render/PsyX_render.cpp
    FIND    "\t\t\"\tvec4 dither(vec4 color) {\\n\"\\\n\t\t\"\t\tivec2 dc"
    REPLACE "\t\t\"\tuniform float ditherScale;\\n\"\\\n\t\t\"\tvec4 dither(vec4 color) {\\n\"\\\n\t\t\"\t\tivec2 dc")

psycross_fixup(gpu/PsyX_GPU.cpp
    FIND    "\tGR_SetBlendMode(split.blendMode);\n\n\tGR_DrawTriangles(split.startVertex, split.numVerts / 3);"
    REPLACE "\tGR_SetBlendMode(split.blendMode);\n\n\tPsyX_PortSetDitherScale(drawOnScreen);\n\tGR_DrawTriangles(split.startVertex, split.numVerts / 3);")

psycross_fixup(gpu/PsyX_GPU.cpp
    FIND    "static void AddSplit(bool semiTrans, bool textured)\n{"
    REPLACE "extern \"C\" void PsyX_PortSetDitherScale(int onScreen);\n\nstatic void AddSplit(bool semiTrans, bool textured)\n{")

# Screenshot key (F12): PsyCross saved one upside-down SCREENSHOT.BMP in
# the working folder, read after the frame was shown. The port takes it
# from the next frame before it is shown (port/psycross/psyx_port.cpp) and
# saves numbered PNGs in "screenshots" next to the exe.
psycross_fixup(PsyX_main.cpp
    FIND    "void PsyX_TakeScreenshot()\n{"
    REPLACE "extern \"C\" void PsyX_PortRequestScreenshot(void);\n\nvoid PsyX_TakeScreenshot()\n{")

psycross_fixup(PsyX_main.cpp
    FIND    "\t\t\tPsyX_TakeScreenshot();\n"
    REPLACE "\t\t\tPsyX_PortRequestScreenshot();\n")

psycross_fixup(render/PsyX_render.cpp
    FIND    "#if defined(RENDERER_OGL) || defined(RENDERER_OGLES)\n\tSDL_GL_SwapWindow(g_window);"
    REPLACE "#if defined(RENDERER_OGL) || defined(RENDERER_OGLES)\n\tPsyX_PortBeforeSwap();\n\tSDL_GL_SwapWindow(g_window);")

# The background fill clears only the view, so the bars stay black.
psycross_fixup(render/PsyX_render.cpp
    FIND    "\tglClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);\n\tglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);"
    REPLACE "\tglClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);\n\tPsyX_PortClear();")

psycross_fixup(render/PsyX_render.cpp
    FIND    "GR_SetViewPort(0, 0, g_windowWidth, g_windowHeight);"
    REPLACE "PsyX_PortSetView();")

psycross_fixup(render/PsyX_render.cpp
    FIND    "\tconst float flipOffset = g_windowHeight - clipRectH * (float)g_windowHeight;\n\tconst float crx = clipRectX * (float)g_windowWidth;\n\tconst float cry = clipRectY * (float)g_windowHeight;\n\tconst float crw = clipRectW * (float)g_windowWidth;\n\tconst float crh = clipRectH * (float)g_windowHeight;\n\n\tglScissor(crx, flipOffset - cry, crw, crh);"
    REPLACE "\tPsyX_PortScissor(clipRectX, clipRectY, clipRectW, clipRectH);")

psycross_fixup(render/PsyX_render.cpp
    FIND    "glBlitFramebuffer(0, 0, g_windowWidth, g_windowHeight, x, y + h, x + w, y, GL_COLOR_BUFFER_BIT, GL_NEAREST);"
    REPLACE "PsyX_PortBlitFromWindow(x, y, w, h);")

psycross_fixup(render/PsyX_render.cpp
    FIND    "\tif (g_lastBoundTexture == texture) {\n\t\treturn;\n\t}"
    REPLACE "\tstatic int portLastFilter = -1;\n\n\tif (g_lastBoundTexture == texture && portLastFilter == PsyX_PortTextureFilter()) {\n\t\treturn;\n\t}\n\n\tportLastFilter = PsyX_PortTextureFilter();")

psycross_fixup(render/PsyX_render.cpp
    FIND    "glUniform1i(bilinearFilterLoc, g_cfg_bilinearFiltering);"
    REPLACE "glUniform1i(bilinearFilterLoc, PsyX_PortTextureFilter());")

psycross_fixup(render/PsyX_render.cpp
    FIND    "\tstrcat(extra_fs_defines, \"#define FRAGMENT\\n\");"
    REPLACE "\tstrcat(extra_fs_defines, \"#define FRAGMENT\\n\");\n\tif (!g_cfg_dithering)\n\t{\n\t\tstrcat(extra_fs_defines, \"#define NO_DITHERING\\n\");\n\t}")

psycross_fixup(render/PsyX_render.cpp
    FIND    "\t\t\"\tvec4 dither(vec4 color) {\\n\"\\\n"
    REPLACE "\t\t\"\tvec4 dither(vec4 color) {\\n\"\\\n\t\t\"#ifdef NO_DITHERING\\n\"\\\n\t\t\"\t\treturn color;\\n\"\\\n\t\t\"#endif\\n\"\\\n")

# GNU C: zero-length array object.
game_fixup(Game/DEBUG.c
    FIND    "DebugMenuLine debugSoundMenu[0];"
    REPLACE "DebugMenuLine debugSoundMenu[1];")
