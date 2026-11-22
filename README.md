# Rewraithed

# Description

***Rewraithed*** is a work-in-progress PC port of Soul Reaver which aspires to eventually become a "Deleted Scenes" rendition of the game, featuring an extra "Uncut" mode where you'll get to experience the story closer to how it was originally envisioned with all planned reavers, glyphs, forges, boss fights, and more content that was excised from the game's development cycle, including the original ending. This will be achieved via performing extensive research into the aforementioned content, and by providing a competent software development kit with a full-featured level editor capable of modifying any aspect of the game world.

While the [decompilation](https://github.com/fmil95/soul-re) that this port is based on wasn't done with AI and is in fact one of the oldest matching decomps for PSX, this project uses coding agents under an *"AI is the shield, humans are the spear"* motto. In this case, AI is used to expedite the porting and SDK development processes in order to allow more time for testing and devising strategies for reinserting cut content into the game without disrupting gameplay balance and filling any necessary gaps along the way for the stuff that remained incomplete in the early console prototypes. 

The port currently works only with the **1999-07-14** build of the game, for which you'll have to provide your own dump of the prototype.

# Building

## Requirements

- Windows*
- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.24 or newer
- Python 3
- Git

*The port is currently available for Microsoft Windows only. Testers are required for a potential Linux version.

## Steps

Clone the repository with its submodules:

```
git clone --recursive https://github.com/fmil95/rewraithed.git
```

Configure a 32-bit build:

```
cmake -S . -B build -A Win32
```

Build:

```
cmake --build build --config Release
```

You set config to be Debug instead of Release to compile an unoptimized binary. The executables are written to the `build/<config>` path. SDL2 and OpenAL Soft are downloaded automatically during configuration.
