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
