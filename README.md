# Dethrace

[![Workflow](https://github.com/dethrace-labs/dethrace/actions/workflows/workflow.yaml/badge.svg)](https://github.com/dethrace-labs/dethrace/actions/workflows/workflow.yml)
[![Twitter](https://badgen.net/badge/icon/twitter?icon=twitter&label)](https://twitter.com/dethrace_labs)
[![Discord Carmageddon server](https://badgen.net/badge/icon/discord?icon=discord&label)](https://discord.gg/f5StsuP)

Dethrace is an attempt to learn how the 1997 driving/mayhem game [Carmageddon](https://en.wikipedia.org/wiki/Carmageddon) works behind the scenes and rebuild it to run natively on modern systems.

## Status

<img width="50%" src="https://raw.githubusercontent.com/dethrace-labs/reccmp-report/refs/heads/main/progress.svg">

### Vulkan, DLSS, and frame generation branch

The `feature/vulkan-renderer` branch adds an experimental third renderer,
`vkrend`, selected with `--vulkan`. The software and OpenGL renderers remain
available.

The current Vulkan path includes:

- Stored and immediate 3D models, textures, depth, translucent geometry, and
  the original 2D HUD composite.
- A widescreen 3D scene with the original 4:3 interface kept undistorted.
- Per-pixel ambient and directional lighting.
- Two-range sun shadow maps. Static world models and cars cast shadows from
  their real geometry; the near range gives cars a sharper outline.
- Filtered opaque textures while colour-keyed sprites keep their original
  point sampling.
- NVIDIA Streamline integration for DLSS Super Resolution, Reflex/PCL, and
  DLSS Frame Generation.
- A standalone tool for decrypting original `.TXT` assets. Dethrace can read
  the resulting plaintext files.

Frame generation currently requests one generated frame per rendered frame,
which is **2x FG**. Vulkan multi-frame generation at 3x or 4x is not enabled.
The integration has produced sustained 2x output in live tests, including after
a swapchain rebuild, but it is still experimental and must be checked from the
runtime counters described below.

Open work includes formal motion-vector debug validation, saved image-quality
and ghosting comparisons, latency measurements, and moving the current
environment-variable controls into normal configuration or UI settings.

## Building

### Dependencies

Dethrace uses CMake to build and SDL2 at runtime. The easiest way to install them is via your favorite package manager.

OSX:

```sh
brew install SDL2 cmake
```

Linux:

```sh
apt-get install libsdl2-dev cmake
```

### Clone

Dethrace uses [git submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules). To clone this fork and its Vulkan BRender branch:

```sh
git clone --branch feature/vulkan-renderer --recurse-submodules https://github.com/xrip/dethrace-dlss-raytrace.git
cd dethrace-dlss-raytrace
```

For an existing checkout, run `git submodule update --init --recursive` after
changing branches.

### Build

Dethrace uses [cmake](https://cmake.org/)

To generate the build files:

```sh
mkdir build
cd build
cmake ..
```

Once cmake has generated the build files for your platform, run the build. For example:

```sh
make
```

### Windows Vulkan + Streamline build

DLSS and frame generation currently require Windows, the Vulkan SDK, Ninja, a
C/C++ compiler, and a local NVIDIA Streamline SDK. The Streamline root must
contain `include/sl.h` and `bin/x64/`.

Configure and build the full renderer with PowerShell:

```powershell
cmake -S . -B cmake-build-streamline -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DDETHRACE_STREAMLINE=ON `
  -DDETHRACE_STREAMLINE_ROOT=C:/path/to/streamline
cmake --build cmake-build-streamline --parallel 8
```

Before building, place the matching licensed Streamline and NGX runtime files
in the SDK's `bin/x64/` directory. The required set includes
`sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, `sl.dlss_g.dll`,
`sl.pcl.dll`, `sl.reflex.dll`, `nvngx_dlss.dll`, `nvngx_dlssg.dll`,
`_nvngx.dll`, and `NvLowLatencyVk.dll`. The build copies `bin/x64/` beside
`dethrace.exe`. These NVIDIA files are not stored in this repository.

## Running the game

Dethrace does not ship with any content. You'll need access to the data from the original game. If you don't have an original CD then you can [buy Carmageddon from GoG.com](https://www.gog.com/game/carmageddon_max_pack).

`dethrace` also supports the various freeware demos:

- [Original Carmageddon demo](https://rr2000.cwaboard.co.uk/R4/PC/carmdemo.zip)
- [Splat Pack demo](https://rr2000.cwaboard.co.uk/R4/PC/splatdem.zip)
- [Splat Pack Xmas demo](https://rr2000.cwaboard.co.uk/R4/PC/Splatpack_christmas_demo.zip)

Dethrace generally expects to be placed into the top level Carmageddon folder. You know you have the right folder when you see the original `CARMA.EXE` there. If you are on Windows, you must also place `SDL2.dll` in the same folder.

### Full Vulkan configuration: DLSS + 2x FG

This is the full 1080p feature profile and the known-good DLSS ratio for 2x
upscaling: a 960x540 3D scene is reconstructed to 1920x1080 in Performance
mode, then DLSS-G requests one generated frame per host frame.

```powershell
$env:DETHRACE_ROOT_DIR = "$PWD\Splat" # Or another Carmageddon game directory
$env:DETHRACE_VULKAN_SCENE_SIZE = "960x540"
$env:DETHRACE_STREAMLINE = "1"
$env:DETHRACE_DLSS = "1"
$env:DETHRACE_DLSSG = "1"
$env:DETHRACE_DLSS_MODE = "performance"

.\cmake-build-streamline\dethrace.exe --vulkan --window `
  --window-width=1920 --window-height=1080 `
  --quick-race-save -nocutscenes 2> fg.log
```

`--quick-race-save` starts the race from an existing career save. Remove it for
normal menu startup. Add `-nosound` when testing on a system without working
audio.

The scene size must agree with the DLSS mode and output aspect ratio. Useful
1920x1080 pairs are:

| Mode | 3D scene | Output | Scale |
|---|---:|---:|---:|
| `quality` | 1280x720 | 1920x1080 | 1.5x |
| `performance` | 960x540 | 1920x1080 | 2x |
| `ultra-performance` | 640x360 | 1920x1080 | 3x |
| `dlaa` | 1920x1080 | 1920x1080 | native |

Do not use a 4:3 scene size with a 16:9 output. A non-uniform scale can make
DLSS return success without writing a valid image.

To confirm that frame generation is really producing frames:

```powershell
Select-String -Path fg.log -Pattern "DLSS-G state"
```

A working 2x run approaches twice as many `presented` frames as
`host-presents`, for example `presented=60 host-presents=30`. If both counters
are equal, Streamline and DLSS-G may be loaded, but interpolation is not active.
`max-generated` is only a capability value and does not prove that generated
frames were presented.

The main runtime controls are:

| Variable | Meaning |
|---|---|
| `DETHRACE_VULKAN_SCENE_SIZE` | Independent `<width>x<height>` size for the 3D scene |
| `DETHRACE_STREAMLINE` | Master Streamline switch |
| `DETHRACE_DLSS` | DLSS Super Resolution |
| `DETHRACE_DLSSG` | DLSS Frame Generation; currently 2x only |
| `DETHRACE_DLSS_MODE` | `quality`, `balanced`, `performance`, `ultra-performance`, or `dlaa` |
| `DETHRACE_STREAMLINE_DEBUG` | Full Streamline SDK logging |
| `DETHRACE_STREAMLINE_CONSOLE` | Streamline debug console |
| `DETHRACE_STREAMLINE_PATH` | Alternate path to `sl.interposer.dll` |

### Decrypting original TXT assets

The standalone script defaults to a safe dry run and verifies every file by
re-encrypting it in memory before it writes anything:

```powershell
python tools\decrypt_game_data.py C:\Games\Carmageddon
python tools\decrypt_game_data.py C:\Games\Carmageddon --write
```

It supports mixed retail cipher methods, the demo cipher, and both inline and
full-line comments.

### Configuration INI file

Alternatively, you may configure a different Carmageddon directory and settings by providing a [dethrace.ini file](docs/CONFIGURATION.md).

### CD audio

Dethrace supports the GOG cd audio convention. If there is a `MUSIC` folder in the Carmageddon folder containing files `Track02.ogg`, `Track03.ogg` etc, then Dethrace will use those files in place of the original CD audio functions.

<img width="571" alt="Screenshot 2024-09-30 at 8 31 59 AM" src="https://github.com/user-attachments/assets/cec72203-9156-4c2a-a15a-328609e65c68">

## Background

Watcom debug symbols for an earlier internal build [were discovered](http://1amstudios.com/2014/12/02/carma1-symbols-dumped) named `DETHRSC.SYM` on the [Carmageddon Splat Pack](http://carmageddon.wikia.com/wiki/Carmageddon_Splat_Pack) expansion CD release. The symbols unfortunately did not match any known released executable, meaning they were interesting but not immediately usable to reverse engineer the game.

This is what it looked like from the Watcom debugger - the names of all the methods were present but the code location they were pointing to was junk:

![watcom-debugger](http://1amstudios.com/img/watcom-debugger.jpg)

We are slowly replacing the original assembly code with equivalent C code, function by function.

### Is "dethrace" a typo?

No, well, I don't think so at least. The original files according to the symbol dump were stored in `c:\DETHRACE`, and the symbol file is called `DETHSRC.SYM`. Maybe they removed the "a" to be compatible with [8.3 filenames](https://en.wikipedia.org/wiki/8.3_filename)?

## Contributing
See [docs](./CONTRIBUTING.md)

## Changelog

[From the beginning until release](docs/CHANGELOG.md)

## Credits

- CrayzKirk (did the first manual matching up functions and data structures in the DOS executable to the debugging symbols and proved it was possible!)
- The developer at Stainless Software who left an old debugging .SYM file on the Splat Pack CD ;)
- https://github.com/isledecomp/reccmp tooling

## Legal

Dethrace is released to the Public Domain. The documentation and function provided by Dethrace may only be utilized with assets provided by ownership of Carmageddon.

The source code in this repository is for non-commerical use only. If you use the source code you may not charge others for access to it or any derivative work thereof.

Dethrace and any of its' maintainers are in no way associated with or endorsed by SCi, Stainless Software or THQ Nordic.
