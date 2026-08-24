# Dethrace Vulkan handoff

## State at transfer

- Repository: `C:\Users\xr1p\CLionProjects\dethrace-vulkan`
- Branch: `feature/vulkan-renderer`
- Root work is based on commit `6ca4de2` (`feat(streamline): add DLSS and frame generation bridge`).
- BRender submodule commit: `15d62bd` (`wip(vkrend): advance DLSS-G device integration`).
- Current work is a checkpoint. DLSS Super Resolution runs, but DLSS Frame Generation does not yet make generated frames.
- Do not call Stage 5 or Stage 6 complete.

## Project purpose and target

This branch adds a modern Vulkan renderer to Dethrace, the open Carmageddon engine. The long-term target is:

1. Keep the old game rules, physics, input, and asset format.
2. Draw the real 3D models through Vulkan.
3. Keep the software and OpenGL paths working.
4. Give Vulkan separate render and display sizes, sampled depth, motion vectors, jitter, and separate scene/UI layers.
5. Use NVIDIA Streamline 2.12 for DLSS Super Resolution and Vulkan DLSS Frame Generation.
6. Turn the old 640x480 scene into a clean HD output without camera jitter, car trails, HUD warping, or changes to game speed.

`VK-PLAN.md` is the design plan. It has stages 0 through 6.

## Stage status

### Stage 3: Vulkan 3D parity

The Vulkan 3D renderer is implemented and committed. It draws stored models, textures, ordered/translucent geometry, depth, the scene, and the 2D composite. The user accepted this as the working Vulkan milestone after testing car damage and repair.

The original Stage 3 checklist is not fully closed. Smoke and sparks were not seen in the user test and were put off for later. A full effect-set comparison, full-race validation run, and final Vulkan/OpenGL FPS table should still be done before a release claim.

Important root commits include:

- `3000d4d` - Stage 2 renderer base.
- `0d52d65`, `a9b1bcb` - Stage 3 geometry and depth work.
- `a376ef0`, `89bd70c` - Stage 3 state and validation notes.

Important BRender commits include:

- `46177e4` - Vulkan driver base.
- `22bd6a1`, `b00204d`, `70d0f1e` - textured and ordered 3D work.

### Stage 4: DLSS input data

The main Stage 4 data path is implemented:

- The renderer has separate scene and display sizes.
- Depth can be sampled.
- The main pass writes `R16G16_SFLOAT` motion vectors.
- Motion history uses actor, geometry, group, and target identity.
- First use, old entries, 2D work, and extra same-frame passes get zero motion.
- The projection has an eight-step Halton jitter path and keeps an unjittered form for Streamline.
- Scene colour, depth, motion, scaling output, overlay/UI, and swapchain resources are exposed to the bridge.

This work is mainly in BRender commits `596aa7f`, `de04d44`, `d300c3e`, and `3840a1f`.

Stage 4 is implemented in code, but its formal acceptance test is not complete. There is no finished bindable motion-vector debug view with recorded checks for camera pan, a moving car, and zero-motion 2D layers. Treat that as the last Stage 4 gate.

### Stage 5: DLSS Super Resolution

The optional Windows Streamline bridge is present. It loads `sl.interposer.dll`, asks for DLSS, tags input colour/output colour/depth/motion, sets constants, and calls DLSS evaluation. Plain Vulkan remains the fallback when the features are not requested or do not load.

Current live proof on an RTX 5060 Ti, driver 610.88:

- Streamline 2.12 and DLSS `v310.7.0` load.
- The scene target is 640x480 and the swapchain/output is 1280x720.
- DLSS reports loaded and the frame contract reports `SR=on`.

Stage 5 is not complete because there is no saved 1080p A/B image check, no ghosting check behind moving cars, and no measured FPS gain. There is also no INI or command-line DLSS setting yet; the bridge uses environment variables only.

### Stage 6: DLSS Frame Generation

The current checkpoint adds:

- Streamline Win32/Vulkan surface and swapchain hook use.
- Vulkan device features and extensions needed by the current NGX kernel.
- Reflex setup and sleep.
- PCL Simulation, RenderSubmit, and Present markers around the real game frame.
- HUD-less colour tagging and DLSS-G state counters.
- A plain Vulkan path that does not install Streamline hooks when DLSS and FG are off.

DLSS-G loads and returns status 0, but it does not interpolate. The last 35-second live run reported `750 presented / 750 host-presents`, with `max-generated=5`. A working run must have more presented frames than host presents. Stage 6 is not complete.

## Current architecture

### Game and harness

- `src/harness/harness.c` selects the platform and renderer. `--vulkan` selects `vkrend`.
- `src/harness/platforms/sdl2.c` creates the SDL Vulkan window and surface.
- On Windows with Streamline active, SDL gives the native `HWND` and `HINSTANCE` to the bridge so the surface is made through the Streamline hook.
- `src/DETHRACE/common/mainloop.c` starts the Streamline frame before game input/simulation and ends the simulation marker before rendering.

### Vulkan renderer

- `lib/BRender-v1.3.2/drivers/vkrend/` owns the Vulkan instance, device, queues, swapchain, command buffers, frame sync, pipelines, descriptors, textures, geometry, and scene targets.
- `devpixmp.c` owns acquire, command recording, Streamline evaluation, submit, and present.
- `renderer.c`, `cache.c`, and `gstored.c` build the scene constants, jittered and unjittered matrices, and object motion history.
- The current path renders the old game scene at 640x480 and can present at a larger display size.

### Streamline bridge

- `src/harness/streamline_bridge.cpp` is the Windows C++17 bridge.
- `src/harness/streamline_bridge_stub.c` keeps non-Streamline builds linkable.
- `src/harness/include/harness/streamline_bridge.h` is the C boundary used by the game and BRender.
- Frame resources are passed as small C records. The bridge turns them into Streamline Vulkan resources and uses frame-based resource tags.
- Streamline is built only when `DETHRACE_STREAMLINE=ON`. Other builds use the stub.

## Pending checkpoint changes and why they exist

### Root repository

- `src/DETHRACE/common/mainloop.c`
  - Adds BeginFrame and EndSimulation calls around the real game simulation.
  - This gives Reflex/PCL the right frame order instead of starting only inside rendering.
- `src/harness/include/harness/streamline_bridge.h`
  - Adds Win32 surface creation and game-frame lifecycle entry points.
- `src/harness/platforms/sdl2.c` and `sdl2_syms.h`
  - Read SDL native Win32 window data and make the Vulkan surface through Streamline when active.
- `src/harness/streamline_bridge.cpp`
  - Adds Reflex functions, frame state, PCL lifecycle, DLSS-G counters, delayed state reads, frame-aware tags, and optional verbose logs.
  - Uses the display-size DLSS output as HUD-less colour when SR is on.
  - Avoids loading the Vulkan interposer when neither SR nor FG was asked for.

### BRender submodule (`15d62bd`)

- `drivers/vkrend/devpixmp.c`
  - Keeps validation on for plain Vulkan and turns it off for the hooked Streamline path because Streamline virtual swapchain work gives false validation messages.
  - Recreates the probe surface through Streamline after device creation.
  - Moves PCL markers to match the real command-buffer, queue-submit, and queue-present order.
  - Sets FG state before command recording and reads it on the next frame.
- `drivers/vkrend/vksetup.c`
  - Adds `VK_NVX_binary_import`, `VK_NVX_image_view_handle`, and `VK_KHR_buffer_device_address` when supported and needed by Streamline.
  - Adds Vulkan 1.2 feature checks for timeline semaphores, descriptor indexing, and buffer device address.

## Known faults and likely causes

Work in this order. These are direct integration faults, not image-quality tuning items.

1. **Plain Vulkan has a new validation error.** `drivers/vkrend/vksetup.c` creates `VkPhysicalDeviceVulkan12Features vk12 = {0}` but never sets `vk12.sType`. It is put in the device `pNext` chain, so validation reads type 0 as `VK_STRUCTURE_TYPE_APPLICATION_INFO`. Set `vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES`, rebuild, and run plain Vulkan with validation before any other FG work.
2. **`sl::Constants::renderingGameFrames` is not set.** A zero-made `sl::Constants` leaves this field invalid. The Streamline DLSS-G checklist says it must say whether game frames are being rendered. Set it to `sl::Boolean::eTrue` for the current normal game frame path, then test generated-frame counts.
3. **The Streamline preference flags replace all defaults.** The bridge sets only `eUseFrameBasedResourceTagging`. The SDK log then says several command-list state hooks are not supported and the plug-in may not work. Test `eDisableCLStateTracking | eUseFrameBasedResourceTagging`, which keeps the intended no-state-tracking mode while using frame tags.
4. **Feature checks happen too early.** `DethraceStreamlinePrepare()` asks NGX/plug-ins about feature state before the Vulkan device is ready. Startup logs show `getNGXFeatureRequirements 0xbad00005` and `initializePlugins ... without device being created`. Move support/state checks after `slSetVulkanInfo` and device setup.
5. **Backbuffer extent is not clean.** DLSS-G logs `Invalid backbuffer resource extent ... 0 x 0` and resets it to 1280x720. Restore a correct display extent for the virtual backbuffer path without giving Streamline a false resource handle.
6. **UI input is not yet a true display-size alpha layer.** The current 640x480 upload image is not tagged as UI when output is 1280x720. The DLSS output is used as HUD-less colour. Before FG can be called visually correct, make and tag a display-size UI colour+alpha layer or prove the current composite order is safe.
7. **Camera planes are hard-coded.** The bridge uses near 0.1 and far 10000. Pass the real BRender camera values before final DLSS image tests.
8. **Validation is off under Streamline.** This is a known limit caused by its virtual swapchain resources. Always keep a separate plain Vulkan validation run.
9. **No runtime settings UI.** DLSS and FG are environment-only. The planned INI/CLI controls and safe live toggle do not exist.
10. **Stage 4 and visual proof remain open.** Add the motion debug view, then save HD A/B frames and check smoke, sparks, mirror, map, fog, translucency, moving-car trails, and HUD stability.

## Build setup

The checked build directory is `cmake-build-streamline`:

- Generator: Ninja
- Build type: Release
- C compiler: CLion MinGW GCC
- C++ compiler: CLion MinGW G++
- `DETHRACE_STREAMLINE=ON`
- `DETHRACE_STREAMLINE_ROOT=C:/Temp/dethrace-streamline-sdk`

Configure from PowerShell:

```powershell
cmake -S . -B cmake-build-streamline -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DDETHRACE_STREAMLINE=ON `
  -DDETHRACE_STREAMLINE_ROOT=C:/Temp/dethrace-streamline-sdk
```

Build:

```powershell
cmake --build cmake-build-streamline --parallel 8
```

The output directory must contain the matching Streamline 2.12 plug-ins and NGX DLLs. The current directory has `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, `sl.dlss_g.dll`, `sl.pcl.dll`, `sl.reflex.dll`, `nvngx_dlss.dll`, `nvngx_dlssg.dll`, `_nvngx.dll`, and `NvLowLatencyVk.dll`.

The SDK and copied DLLs are local build inputs. They are not tracked by this repository. Check NVIDIA licence and attribution rules before shipping them.

## Run and check

Game data is in the untracked `Carma/` directory.

Plain Vulkan fallback:

```powershell
$env:DETHRACE_ROOT_DIR = "$PWD\Carma"
$env:DETHRACE_STREAMLINE = "0"
.\cmake-build-streamline\dethrace.exe --vulkan --window `
  --window-width=1280 --window-height=720 --quick-race=0 --fps=30 `
  -nosound -nocutscenes
```

DLSS Quality plus FG request:

```powershell
$env:DETHRACE_ROOT_DIR = "$PWD\Carma"
$env:DETHRACE_STREAMLINE = "1"
$env:DETHRACE_DLSS = "1"
$env:DETHRACE_DLSSG = "1"
$env:DETHRACE_DLSS_MODE = "quality"
$env:DETHRACE_STREAMLINE_DEBUG = "0"
.\cmake-build-streamline\dethrace.exe --vulkan --window `
  --window-width=1280 --window-height=720 --quick-race=0 --fps=30 `
  -nosound -nocutscenes
```

Useful bridge controls:

- `DETHRACE_DLSS_MODE=quality|balanced|performance|ultra-performance|dlaa`
- `DETHRACE_STREAMLINE_DEBUG=1` for full SDK messages
- `DETHRACE_STREAMLINE_CONSOLE=1` for the Streamline console
- `DETHRACE_STREAMLINE_PATH=<path>` to load another interposer
- `DETHRACE_STREAMLINE_APP_ID` and `DETHRACE_STREAMLINE_PROJECT_ID` for approved app data

## Validation done for this handoff

### Build

Command:

```text
cmake --build cmake-build-streamline --parallel 8
```

Result: exit 0, `ninja: no work to do`. Ninja considers the current source built.

### CTest

Command:

```text
ctest --test-dir cmake-build-streamline --output-on-failure
```

Result: exit 0, but `No tests were found!!!`. This build has no registered automated tests.

### Plain Vulkan live check

Run time: 22 seconds at 1280x720 with a 640x480 scene target. The process was stopped by the handoff check, so its exit code was 1 from `taskkill`, not a game crash. Vulkan made the device, swapchain, 2D upload image, and 3D scene target. It also gave the `vk12.sType` validation error listed above.

### DLSS + FG live check

Run time: 35 seconds on NVIDIA GeForce RTX 5060 Ti, driver 610.88. The process was stopped by the handoff check. Results:

- DLSS loaded: yes
- DLSS-G loaded: yes
- Requirement results: 0
- Frame contract: `SR=on FG=on Reflex=on PCL=on mask=3`
- Scene target: 640x480
- Swapchain: 1280x720, mailbox present mode
- Last counter: `presented=750 host-presents=750 max-generated=5`

The feature is loaded but interpolation is off.

### Control sample

NVIDIA's `vk_streamline` sample was built earlier with the same SDK and machine. It gave 53 presented frames for 30 host presents, so the GPU, driver, SDK, and Vulkan FG path can work. Local paths were `C:\Temp\vk_streamline` and `C:\Temp\bin_x64\Release\official_sl.log`. These are temporary files and may be removed later.

### Diff checks and secret review

- Root `git diff --check`: pass before commit; only LF-to-CRLF warnings.
- BRender `git diff --check`: pass before commit; only LF-to-CRLF warnings.
- Added tracked lines: no common secret or private-key patterns found.
- Untracked names: no common secret-key file names found.

## Untracked files left on purpose

Do not add or remove these without the user's order:

- `Carma/` - 3423 game-data files used to run the game.
- `SDL2-2.32.8/` - 440 local SDL source/package files.
- `cmake/zig-toolchain.cmake` - one local toolchain file.
- `voxels/` - 32 user files.

These are not part of the Vulkan/DLSS checkpoint and remain uncommitted.

## Next work, in order

1. Set `vk12.sType`, build, and prove a clean plain Vulkan device-create path.
2. Set `constants.renderingGameFrames = sl::Boolean::eTrue`.
3. Keep `eDisableCLStateTracking` with frame-based tagging.
4. Move feature/NGX checks until after the Vulkan device data is set.
5. Run the same 30 FPS check. The first hard gate is `presented > host-presents`.
6. Fix the 0x0 backbuffer extent warning and give FG a correct display-size HUD-less/UI contract.
7. Add a safe FG off/on path and show that game physics still runs at host-frame speed.
8. Finish the Stage 4 motion-vector debug view and validate camera, car, and 2D motion fields.
9. Save DLSS Quality 1080p A/B images, inspect moving-car trails and HUD stability, and record FPS and latency.
10. Add INI/CLI settings, fallback tests, licence text, and shipping documentation.

## Important files to inspect first

- `src/harness/streamline_bridge.cpp` - Streamline load, options, constants, tags, Reflex, PCL, and telemetry.
- `src/harness/include/harness/streamline_bridge.h` - C frame/resource contract.
- `src/DETHRACE/common/mainloop.c` - simulation marker placement.
- `src/harness/platforms/sdl2.c` and `sdl2_syms.h` - SDL/Win32 surface hook.
- `lib/BRender-v1.3.2/drivers/vkrend/vksetup.c` - Vulkan feature and extension chain; first bug is here.
- `lib/BRender-v1.3.2/drivers/vkrend/devpixmp.c` and `devpixmp.h` - frame targets, resource tags, command submit, and present.
- `lib/BRender-v1.3.2/drivers/vkrend/renderer.c` - scene setup and Streamline camera data.
- `lib/BRender-v1.3.2/drivers/vkrend/cache.c` - projection conversion and jitter.
- `lib/BRender-v1.3.2/drivers/vkrend/gstored.c` - stored-model draw and motion history use.
- `lib/BRender-v1.3.2/drivers/vkrend/brender.vert.glsl` and `brender.frag.glsl` - motion-vector output.
- `CMakeLists.txt` - optional Streamline build.
- `VK-PLAN.md` - stage rules and acceptance checks.
- `docs/RENDERING_PIPELINE.md` - renderer design notes.

## Current objective

Make Vulkan DLSS-G create real generated frames without hiding errors or changing game timing. The next measurable success is a clean Vulkan run plus a DLSS-G counter where presented frames are greater than host presents. After that, inspect the HD image and close the Stage 4/5/6 visual checks.
