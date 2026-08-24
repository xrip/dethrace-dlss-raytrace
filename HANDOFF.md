# Dethrace Vulkan handoff

## State at transfer

- Repository: `C:\Users\xr1p\CLionProjects\dethrace-vulkan`
- Branch: `feature/vulkan-renderer`
- BRender submodule commit: `5b23b62` (`fix(vkrend): make DLSS-G interpolate and stop the jitter shake`).
- DLSS Super Resolution runs. **DLSS Frame Generation now makes real generated frames**: 1372 presented for 690 host presents on an RTX 5060 Ti, driver 610.88, 640x480 scene at 1280x720. That is a clean 2x and it repeats across runs.
- Stage 6's first hard gate (`presented > host-presents`) is met. Stage 5 and Stage 6 are still **not** complete: no saved HD A/B images, no ghosting check, no FPS/latency table, no INI or command-line settings.

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

DLSS-G interpolates. The SDK log reports `DLSS-G interpolation state changed from disabled to enabled (mode=sl::DLSSGMode::eOn, numFramesToGenerate=1)` and the counter reaches `presented=1372 host-presents=690`.

Four faults had to be fixed together to get there. All four are in the BRender submodule (`5b23b62`):

1. `vksetup.c` put `VkPhysicalDeviceVulkan12Features` into the device `pNext` chain with `sType` left at zero. The driver skipped the struct, so `timelineSemaphore`, `descriptorIndexing` and `bufferDeviceAddress` were never actually enabled for Streamline.
2. Freeing a model or a texture outside a scene called `vkDeviceWaitIdle`, and the "deferred" path was really a `QueueWaitIdle` at scene end. A 25-second race made **5887** such stalls, about thirteen per frame. Streamline flushes every DLSS-G worker queue on each one, so interpolation could never build up. Retired objects now carry the frame that released them and are destroyed once that frame's fence has been waited on (`DeviceVkRetireDeferred`). This also lifted the frame rate: the game now holds the 30 FPS cap where it used to sit near 21.
3. `sl.dlss_g` registers its `vkCreateSwapchainKHR` hook when the plug-in starts up, so `DethraceStreamlineSetVulkanPhysicalDevice` now runs **before** `DeviceVkCreateSwapchain`, not after it.
4. The Halton jitter was applied to the projection on every scene, including frames DLSS never resolved. The offset reached the screen as a sub-pixel left/right shake, clearest on distant high-contrast edges. Jitter is now gated on `streamline_sr_active`.

Stage 6 is still not complete: image quality, a safe FG off/on path, and settings work all remain.

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

1. **Backbuffer extent is not clean.** DLSS-G logs `Invalid backbuffer resource extent ... 0 x 0` and resets it to 1280x720. Restore a correct display extent for the virtual backbuffer path without giving Streamline a false resource handle.
2. **UI input is not yet a true display-size alpha layer.** The current 640x480 upload image is not tagged as UI when output is 1280x720. The DLSS output is used as HUD-less colour. Before FG can be called visually correct, make and tag a display-size UI colour+alpha layer or prove the current composite order is safe.
3. **Camera planes are hard-coded.** The bridge uses near 0.1 and far 10000. Pass the real BRender camera values before final DLSS image tests.
4. **FG toggles without recreating the swapchain.** `DethraceStreamlineSetFrameGenerationActive` flips DLSS-G between `eOn` and `eOff` as `scene_has_content` changes, so the SDK logs `DLSS-G interpolation state changed` several times a run. The DLSS-G guide, section 18.0, says the swapchain should be recreated on every such change.
5. **Startup priming makes the FG counter mix menu frames.** `DethraceStreamlineSetVulkanPhysicalDevice` calls `slDLSSGSetOptions(eOn)` to force plug-in startup before the swapchain exists, and it sets `fg_runtime_on` at the same time. The counter therefore also counts menu frames, where FG is genuinely off, so a run that never reaches a race reads `presented == host-presents` and looks like a failure. It also produces `Repeated slDLSSGSetOptions() call for the frame N` warnings. Separate "plug-in primed" from "FG running".
6. **Three `sl.common` Vulkan hooks stay unsupported.** The SDK logs `Hook sl.common:Vulkan:CmdBindPipeline / CmdBindDescriptorSets / BeginCommandBuffer is NOT supported`. Adding `eDisableCLStateTracking` to the preference flags did not remove them. FG works anyway, so this is not blocking, but it is unexplained.
7. **Validation is off under Streamline.** This is a known limit caused by its virtual swapchain resources. Always keep a separate plain Vulkan validation run.
8. **No runtime settings UI.** DLSS and FG are environment-only. The planned INI/CLI controls and safe live toggle do not exist.
9. **Stage 4 and visual proof remain open.** Add the motion debug view, then save HD A/B frames and check smoke, sparks, mirror, map, fog, translucency, moving-car trails, and HUD stability.
10. **Jitter scale has a small oddity.** `set_scene_jitter` computes `width = render_area.extent.width * scale + 0.5f`, where `scale` is `render_scale` again even though `render_area` is already the scene extent. With `render_scale` at 1.0 this only costs a 640 vs 640.5 rounding difference, but it would be wrong at any other render scale.

### Corrections to earlier handoff text

Two items in the previous list were wrong and have been dropped:

- **`sl::Constants::renderingGameFrames` does not exist in Streamline 2.12.** It appears only in the SDK's stale `docs/ProgrammingGuide.md:1210` and `docs/ProgrammingGuideDLSS_G.md:932`; the shipped `include/sl_consts.h` has no such field. Setting it would not compile.
- **`slSetVulkanInfo` must not be called here.** `include/sl_helpers_vk.h:250` says it is only for hosts that do **not** use Streamline's `vkCreateDevice` / `vkCreateInstance` proxies, and `vksetup.c` fetches every entry point through the interposer, so it does use them. The real ordering fault was that `slIsFeatureLoaded` needs the device first (`include/sl_core_api.h:117`); those checks now run in `DethraceStreamlineSetVulkanPhysicalDevice`.

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

`ctest --test-dir cmake-build-streamline` reports `No tests were found!!!`; that build registers no tests. The separate `cmake-build-tests` directory does: `ctest --test-dir cmake-build-tests` runs `test_dethrace` and it passes. Neither covers the Vulkan or Streamline paths.

### Plain Vulkan live check

Run time: 30 seconds at 1280x720 with a 640x480 scene target, validation layer on. The process was stopped by the check, so its exit code came from the kill, not a crash. Vulkan made the device, swapchain, 2D upload image, and 3D scene target, and produced **no validation messages at all**. The old `vk12.sType` error is gone.

### DLSS + FG live check

Run time: 35 seconds on NVIDIA GeForce RTX 5060 Ti, driver 610.88. Results:

- DLSS loaded: yes; DLSS-G loaded: yes; requirement results 0
- Frame contract: `SR=on FG=on Reflex=on PCL=on mask=3`
- Scene target 640x480, swapchain 1280x720, mailbox present mode
- Last counter: `presented=1372 host-presents=690 max-generated=5`
- SDK log: `DLSS-G interpolation state changed from disabled to enabled (mode=sl::DLSSGMode::eOn, numFramesToGenerate=1)`

Interpolation is running at a clean 2x. Repeated across three separate runs.

Caution when reading the counter: because the plug-in is primed at startup, menu frames are counted too. A run that stays in the menus reads `presented == host-presents` and looks like a failure even though FG is fine. Always let the run reach a race.

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

1. Confirm by eye that the left/right shake is gone, in both plain Vulkan and DLSS mode. The jitter gate is a code fix that has not yet had a visual check.
2. Fix the 0x0 backbuffer extent warning and give FG a correct display-size HUD-less/UI contract.
3. Recreate the swapchain when FG is switched on or off, and show that game physics still runs at host-frame speed.
4. Separate plug-in priming from FG runtime state so the counter measures only frames where FG is really on.
5. Pass the real BRender near/far planes instead of the hard-coded 0.1 / 10000.
6. Finish the Stage 4 motion-vector debug view and validate camera, car, and 2D motion fields.
7. Save DLSS Quality 1080p A/B images, inspect moving-car trails and HUD stability, and record FPS and latency.
8. Add INI/CLI settings, fallback tests, licence text, and shipping documentation.

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

DLSS-G now creates real generated frames, so the objective moves from "make it run" to "make it look right". Next: confirm the shake fix by eye, give FG a correct display-size HUD-less/UI contract, recreate the swapchain on FG toggles, then close the Stage 4/5/6 visual checks with saved HD A/B frames and an FPS and latency table.
