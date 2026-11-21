# Rewraithed

# Description

**Rewraithed** is a work-in-progress PC port of Soul Reaver which aspires to eventually become a "Deleted Scenes" rendition of the game, featuring an extra "Uncut" mode where you'll get to experience the story closer to how it was originally envisioned with all planned reavers, glyphs, forges, boss fights, and more content that was excised from the game's development cycle, including the original ending. This will be achieved via performing extensive research into the aforementioned content, and by providing a competent software development kit with a full-featured level editor capable of modifying any aspect of the game world.

While the [decompilation](https://github.com/fmil95/soul-re) that this port is based on wasn't done with AI and is in fact one of the oldest matching decomps for PSX, this project uses coding agents under a *"AI is the shield, humans are the spear"* motto. In this case, AI is used to expedite the porting and SDK development processes in order to allow more time for testing and devising strategies for reinserting cut content into the game without disrupting gameplay balance and filling any necessary gaps along the way for the stuff that remained incomplete in the early console prototypes. 

The port currently works only with the 1999-07-14 build of the game, for which you'll have to provide your own dump of the prototype.

# Building

## Requirements

- Windows
- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.24 or newer
- Python 3
- Git

## Steps

Clone the repository with its submodules:

```
git clone --recursive https://github.com/fmil95/rewraithed.git
cd rewraithed
```

If you already cloned it without `--recursive`:

```
git submodule update --init
```

Configure a 32-bit build (the port only supports 32-bit):

```
cmake -S . -B build -A Win32
```

Build:

```
cmake --build build --config Release
```

`Release` can be replaced with `Debug` or `RelWithDebInfo`. The executables
(`rewraithed.exe` and `rewraithed-launcher.exe`) are written to
`build\<config>\`. SDL2 and OpenAL Soft are downloaded automatically during
configuration.
