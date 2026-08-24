# Dethrace Vulkan renderer — handoff

## 1. What this project is

Dethrace is an open reimplementation of Carmageddon (1997). This branch,
`feature/vulkan-renderer`, adds a third renderer — `vkrend` — beside the existing
software and OpenGL ones, selected with `--vulkan`.

The point of a Vulkan path is not Vulkan for its own sake. It is the only way to
reach NVIDIA Streamline, and through it DLSS Super Resolution and DLSS Frame
Generation. The target is to take a 1997 game that renders at 640x480 and put a
clean, modern, high-refresh HD image on screen without touching game rules,
physics, input, asset formats, or the speed at which the simulation runs.

Constraints that shape every decision here:

1. Game rules, physics, input and asset handling stay untouched.
2. The software and OpenGL renderers must keep working exactly as before.
3. `src/DETHRACE/**` is *reconstructed original code*, verified against the
   original binary by `reccmp` (see `reccmp-project.yml`). Changing a
   reconstructed function breaks that verification. Prefer the harness, the
   driver, or brand-new files.

`VK-PLAN.md` holds the original staged design (stages 0-6) and is still the
reference for the acceptance criteria.

## 2. State at transfer

- Repository: `C:\Users\xr1p\CLionProjects\dethrace-vulkan`
- Branch: `feature/vulkan-renderer`
- Root HEAD: current `feature/vulkan-renderer` handoff commit
- BRender submodule: `9253832`, branch `feature/vulkan-renderer`
- The current fix and its live check are not committed yet (§9).

**DLSS Frame Generation works.** Measured on an RTX 5060 Ti,
driver 610.88, Streamline 2.12, DLSS `v310.7.0`: a 960x540 scene upscaled to
1920x1080 reaches a clean 2x — 30 host presents producing 60 presented frames,
sustained. This was the branch's main open goal and it is now met.

The current working tree also keeps frame generation active across a deliberate
swapchain rebuild. See §8 for the live resize result.

## 3. What changed in this session, and why

### BRender submodule (`15d62bd` → `8540bf3`)

**`5b23b62` — make DLSS-G interpolate, and stop the jitter shake.**
Frame generation loaded, reported healthy, and produced nothing. Four separate
faults had to be fixed together before a single generated frame appeared:

- `vksetup.c` chained `VkPhysicalDeviceVulkan12Features` into the device
  `pNext` list with `sType` left at zero. Drivers walk that chain by `sType`,
  so the struct was skipped and `timelineSemaphore`, `descriptorIndexing` and
  `bufferDeviceAddress` — all needed by the NGX kernel — were never actually
  enabled. This looked like a mere validation warning; it was not.
- Freeing a model or texture called `vkDeviceWaitIdle`, and the so-called
  "deferred" path was a `QueueWaitIdle` at scene end. A 25-second race made
  **5887** such stalls, roughly thirteen per frame. Streamline flushes every
  DLSS-G worker queue on each one, so interpolation could never establish
  itself. Retired objects now carry the frame that released them and die once
  that frame's fence has been waited on (`DeviceVkRetireDeferred`). Side
  benefit: the frame rate rose from about 21 to holding the 30 cap.
- `sl.dlss_g` installs its `vkCreateSwapchainKHR` hook when the plug-in starts
  up, and it starts up on first use. The swapchain was being created before
  that, so DLSS-G never owned the swapchain it was asked to interpolate into.
  Streamline device setup now runs before `DeviceVkCreateSwapchain`.
- The Halton jitter was applied on every scene, including frames DLSS never
  resolved, so the offset reached the screen as a sub-pixel horizontal shake —
  most visible on distant flat surfaces. Jitter is now gated on
  `streamline_sr_active`.

**`122fa8c` — scene renders at its own size and aspect.**
`DETHRACE_VULKAN_SCENE_SIZE=<w>x<h>` decouples the 3D scene from the game's
640x480 screen. This exists because asking DLSS to turn a 4:3 640x480 scene into
16:9 1920x1080 is a 3.0x/2.25x non-uniform stretch that matches no DLSS mode: it
returned `eOk` and wrote nothing, giving a **black screen with no error**.
960x540 is 16:9 and exactly 2x to 1920x1080. The scene target now scales x and y
independently; `render_scale` remains the older uniform down-scale, and both go
through `DeviceVkSceneScale` so nothing computes scene pixels on its own.
`DeviceVkRecordOverlay` fits the 2D layer at its own aspect and centres it, so
the HUD and menus are no longer stretched.

**`1322d89` — ordered draws get their own material.**
Every pedestrian wore the same sprite, switching to it the moment its animation
advanced. An ordered draw is queued while models are walked and flushed only
after all of them have been. `StoredVkRenderGroup` read the material from
`vk_group_info`, which lives on the *geometry*, not the draw. Carmageddon gives
every pedestrian the same `br_material` and swaps its colour map per pedestrian,
so at flush time that one stored state held only the last pedestrian's sprite.
The queued primitive already snapshots the state stack, so the resolved material
is now folded into that snapshot and the state each draw should use is passed
explicitly.

**`797f3d0` — filter opaque textures.**
The game never asks for texture filtering: BRender defaults `prim.filter` and
`prim.mip_filter` to `BRT_NONE` and nothing in `src/DETHRACE` overrides them. So
every surface was point-sampled at mip 0 and the mip chain built on every upload
was never read. Beyond blockiness this made distant surfaces shimmer, which is
temporal noise DLSS cannot settle — it was degrading reconstruction, not just
sharpness. Opaque draws now use linear + full mips + max anisotropy. The
samplers already existed; nothing selected them.

**`8540bf3` — hand texture descriptor sets back to the pool.**
A texture's descriptor set names the image view it was written with, so
replacing the image abandons the set. Nothing ever freed one. The pool is
created with `FREE_DESCRIPTOR_SET_BIT` and holds 16384, so a long enough race
emptied it, after which `BufferStoredVkBind` failed and the caller *skipped the
draw* — textured objects would simply start going missing. Sets now retire
against the frame fence. Measured over a 120-second race the live count sits
flat at 147 instead of climbing.

### Root repository (`8f05d0d` → `7430f40`)

**`bd738ea` — Streamline bridge corrections.** `slIsFeatureLoaded` requires the
Vulkan device to exist (`sl_core_api.h:117`), so the feature-loaded and
requirements queries moved out of `DethraceStreamlinePrepare` into
`DethraceStreamlineSetVulkanPhysicalDevice`, which also primes `sl.dlss_g` so
its swapchain hook is installed before the caller creates the swapchain.
`eDisableCLStateTracking` is part of the SDK's default preference flags and was
being dropped by assigning only the tagging flag.

**`a0bf529` — widescreen camera aspect.** A wider scene target alone just
stretches the same 4:3 picture; the camera has to agree. BRender's
`field_of_view` is vertical and horizontal view is derived as
`atan(tan(fov/2) * aspect)` (`depth.c:294`), so setting the forward cameras'
aspect to the scene aspect gives **Hor+** widescreen — same vertical view, more
at the sides — with no FOV retuning. The game's own culling, horizon and sky all
read `camera->aspect`, so they follow automatically.

**`60bcd97` — save-driven quick race**, plus an `fflush(stdout)` in
`debug_printf`. See §8.

### Current working tree after takeover

**Swapchain rebuild keeps the Streamline VSync rule.** `recreate_swapchain`
now passes `s->streamline_active ? BR_FALSE : BR_TRUE`, the same rule as first
creation. `tools/verify_streamline_resize.ps1` starts the proven 2x setup,
changes the window client width by two pixels, and checks the rebuilt present
mode and the later DLSS-G counter delta.

**Vulkan lighting and shadow baseline.** The legacy projected car shadow is
working again. Vulkan model and textured-model shaders now apply a small
per-pixel ambient/directional term to 3D materials. The experimental contact
shadow pass remains disabled until its depth sampling is made stable.

## 4. Architecture and design decisions

### Renderer selection
`src/harness/harness.c` parses `--vulkan` and selects `vkrend`;
`src/harness/platforms/sdl2.c` creates the SDL Vulkan window and surface. On
Windows with Streamline active, SDL hands the native `HWND`/`HINSTANCE` to the
bridge so the surface is created through Streamline's hook.

### The Vulkan driver
`lib/BRender-v1.3.2/drivers/vkrend/` owns instance, device, swapchain, command
buffers, sync, pipelines, descriptors, textures, geometry and scene targets. Every
Vulkan entry point is fetched through the harness-supplied
`vkGetInstanceProcAddr`, which is how Streamline gets interposed without the
driver knowing about it.

Two decisions worth understanding before changing anything:

- **Deferred destruction, not device stalls.** Retired images, buffers and
  descriptor sets carry the frame that released them and are destroyed once that
  frame's fence has been waited on. Reintroducing a `vkDeviceWaitIdle` on the
  free path will silently kill frame generation (§3, `5b23b62`).
- **The scene target is not the game's screen.** 3D goes to `scene_image`, the
  2D/UI layer to `upload_image`. They are separate so Streamline can be given
  scene colour, depth, motion and UI independently.

### The Streamline bridge
`src/harness/streamline_bridge.cpp` (Windows, C++17) with
`streamline_bridge_stub.c` keeping non-Streamline builds linkable, behind the C
boundary in `src/harness/include/harness/streamline_bridge.h`. Built only when
`DETHRACE_STREAMLINE=ON`.

### Widescreen without touching reconstructed code
The camera aspect is reasserted once per frame from the platform swap
(`sdl2.c`), not at camera creation, because the game rewrites that field on every
view change. `init.c` is deliberately untouched so `reccmp` still matches it.
`gRearview_camera` is the wing mirror and takes its aspect from the car's mirror
rectangle, so it is excluded.

## 5. What works

- Vulkan 3D rendering: stored models, textures, ordered/translucent geometry,
  depth, scene target, 2D composite.
- Plain Vulkan runs clean under the validation layer with zero messages.
- DLSS Super Resolution.
- DLSS Frame Generation at a sustained 2x, including after a swapchain rebuild.
- Widescreen scene mode with an undistorted 4:3 UI.
- Filtered opaque geometry.
- Legacy projected shadows for cars.
- Per-pixel ambient/directional lighting for Vulkan 3D models.
- `--quick-race-save` for reproducible test runs from a real career.

## 6. What is incomplete

- **Stage 4** has no motion-vector debug view, so camera/car/2D motion fields
  have never been formally validated.
- **Stage 5** has no saved 1080p A/B images, no ghosting check behind moving
  cars, no measured FPS or latency table.
- **Stage 6** has no safe FG on/off path and no proof that physics still runs at
  host-frame speed with FG on.
- **Settings**: DLSS and FG are environment-variable only. No INI, no CLI, no
  in-game UI.
- Smoke and sparks were never confirmed against the OpenGL path.
- Contour shadows for arbitrary world models are not implemented yet; the
  contact-shadow prototype is kept disabled because it produced false dark
  outlines.
- `docs/RENDERING_PIPELINE.md:84-87` is stale: it still says Streamline and
  DLSS evaluation are not present.

## 7. Known issues, in priority order

1. **Fixed in the current working tree: a swapchain rebuild turned frame
   generation off.** `recreate_swapchain` now uses the same Streamline VSync
   rule as first creation. The live resize check rebuilt from present mode 1 to
   present mode 1 and measured 651 presented frames for 330 host presents after
   resize (1.97x). The fix and check are not committed yet.

2. **Sprites cannot be filtered, and bleeding will not fix it.** Colour-key
   transparency is "RGB is black" (`textured_model.frag:59`), so colour-keyed
   draws stay point-sampled. An attempt to convert the key to a real alpha
   channel with RGB bled from opaque neighbours was written, tested and
   **reverted**: black contours appeared on some pedestrian animation frames and
   around smoke. The cause is not failed bleeding — Carmageddon's sprites were
   drawn anti-aliased against black, so their edges contain genuinely near-black
   texels that were never transparent under either the old shader or the new
   one. `NEAREST` hid them at one texel wide; `LINEAR` spreads each over 2-3
   screen pixels and they become visible outlines. Smoke shows it worst because
   its edges are softest. Three options, none chosen:
   (a) leave it; (b) widen the key threshold to "dark enough", which risks
   eating legitimate dark detail and needs several rounds of visual checking;
   (c) premultiplied alpha with real blending, which is the correct answer but
   turns sprites from discard to blended and makes them depth-sort sensitive.

3. **The FG counter mixes menu frames and produces false alarms.** The plug-in
   is primed at startup, so loading and menu frames are counted while FG is
   genuinely off. A run that has not reached a moving race reads
   `presented == host-presents` and looks exactly like a broken feature. This
   caused two false alarms in one session. Judge the **last** samples, not the
   totals: 30 host presents becoming 60 presented is FG working. Allow 90+
   seconds. Separating "plug-in primed" from "FG running" would remove the trap.

4. **DLSS mode must be matched to the upscale ratio by hand.** Nothing checks
   that `DETHRACE_DLSS_MODE` agrees with the scene-to-display ratio, and a large
   mismatch gives a black frame with **no error** — `slEvaluateFeature` still
   returns `eOk`, so the renderer blits an untouched scaling output. Either
   derive the mode from the ratio or query `slDLSSGetOptimalSettings`.

5. **World-anchored 2D markers are not re-projected.** The 2D layer holds both
   the HUD and markers placed from world positions (damage and cop indicators).
   The scene is wider than the 4:3 UI box, so those markers sit in the wrong
   place. Deliberately deferred when widescreen landed.

6. **Backbuffer extent is not clean.** DLSS-G logs `Invalid backbuffer resource
   extent ... 0 x 0` and resets it to the display size.

7. **UI is not a true display-size alpha layer.** The 640x480 upload image is
   not tagged as UI at 1920x1080; the DLSS output is used as HUD-less colour.

8. **Camera planes are hardcoded** in the bridge at near 0.1 / far 10000
   instead of the real BRender values.

9. **Validation is off under Streamline**, because its virtual swapchain
   resources produce false positives. Always keep a separate plain Vulkan
   validation run — it is currently clean and should stay that way.

10. **Three `sl.common` Vulkan hooks report unsupported** (`CmdBindPipeline`,
    `CmdBindDescriptorSets`, `BeginCommandBuffer`). Adding
    `eDisableCLStateTracking` did not remove them. FG works regardless; the
    reason is unexplained.

11. **Only save slot 0 is loadable** with the shipped data.
    `LoadSavedGames()` rejects any file whose size is not `sizeof(tSave_game)`
    (948 bytes); `SAVE1`..`SAVE9` are 1464 bytes. Pre-existing, not caused by
    this branch.

### Traps that cost real time — do not repeat

- **`sl::Constants::renderingGameFrames` does not exist in Streamline 2.12.** It
  appears only in the SDK's own stale `docs/`, not in `include/sl_consts.h`.
  Setting it does not compile. Trust the headers over the bundled docs.
- **Do not call `slSetVulkanInfo`.** `sl_helpers_vk.h:250` restricts it to hosts
  that do *not* use Streamline's device/instance proxies, and `vksetup.c` does
  use them.
- **Do not free descriptor sets from an earlier generation.**
  `DeviceVkSceneDestroy` destroys and rebuilds the pool — that is what
  `texture_descriptor_generation` signals — so those handles name a pool that no
  longer exists. Freeing them crashes intermittently, well after the fact. It
  cost a bisect to find.
- **Do not read `renderer->state.current` unconditionally for a draw's
  material.** For geometry with no material it picks up whatever colour map the
  current state holds, which can outlive the `br_buffer_stored` it names.
  Crashes on an access violation.

## 8. Build, run, test, verify

### Configure and build

```powershell
cmake -S . -B cmake-build-streamline -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DDETHRACE_STREAMLINE=ON `
  -DDETHRACE_STREAMLINE_ROOT=C:/Temp/dethrace-streamline-sdk
cmake --build cmake-build-streamline --parallel 8
```

Toolchain: Ninja, Release, CLion MinGW GCC/G++. The output directory must hold
the matching Streamline 2.12 plug-ins and NGX DLLs (`sl.interposer.dll`,
`sl.common.dll`, `sl.dlss.dll`, `sl.dlss_g.dll`, `sl.pcl.dll`, `sl.reflex.dll`,
`nvngx_dlss.dll`, `nvngx_dlssg.dll`, `_nvngx.dll`, `NvLowLatencyVk.dll`); the
build copies them. The SDK and those DLLs are local build inputs, are not
tracked here, and are subject to NVIDIA licence and attribution terms before
shipping.

The `cmake-build-stage3-debug` directory currently **fails to build** on the
`dethrace.rc` resource step (`CreateProcess failed`), so there is no working
symbolised build. That made one crash this session much harder to diagnose than
it needed to be; fixing it is worth an hour.

### Tests

```powershell
ctest --test-dir cmake-build-tests --output-on-failure
```

Note the directory: `cmake-build-streamline` registers **no** tests. Neither
suite covers the Vulkan or Streamline paths — all renderer verification is by
live run.

### Run

Plain Vulkan, validation layer active:

```powershell
$env:DETHRACE_ROOT_DIR = "$PWD\Carma"
$env:DETHRACE_STREAMLINE = "0"
.\cmake-build-streamline\dethrace.exe --vulkan --window `
  --window-width=1280 --window-height=720 --quick-race-save `
  -nosound -nocutscenes
```

Widescreen + DLSS + FG, 960x540 upscaled 2x to 1920x1080:

```powershell
$env:DETHRACE_ROOT_DIR = "$PWD\Carma"
$env:DETHRACE_VULKAN_SCENE_SIZE = "960x540"
$env:DETHRACE_STREAMLINE = "1"; $env:DETHRACE_DLSS = "1"; $env:DETHRACE_DLSSG = "1"
$env:DETHRACE_DLSS_MODE = "performance"
.\cmake-build-streamline\dethrace.exe --vulkan --window `
  --window-width=1920 --window-height=1080 --quick-race-save `
  -nosound -nocutscenes 2> fg.log
```

Match the mode to the ratio: 2x is `performance`, 3x `ultra-performance`,
1.5x `quality`.

Settings, all environment variables:

| Variable | Meaning |
|---|---|
| `DETHRACE_VULKAN_SCENE_SIZE` | `<w>x<h>` 3D scene size; also sets camera aspect |
| `DETHRACE_VULKAN_RENDER_SCALE` | older uniform down-scale, 0.5-1.0 |
| `DETHRACE_STREAMLINE` | master on/off for the bridge |
| `DETHRACE_DLSS` / `DETHRACE_DLSSG` | Super Resolution / Frame Generation |
| `DETHRACE_DLSS_MODE` | `quality`/`balanced`/`performance`/`ultra-performance`/`dlaa` |
| `DETHRACE_STREAMLINE_DEBUG` | full SDK logging |
| `DETHRACE_STREAMLINE_CONSOLE` | Streamline console |
| `DETHRACE_STREAMLINE_PATH` | alternate interposer |
| `DETHRACE_STREAMLINE_APP_ID` / `_PROJECT_ID` | approved application identity |

`--quick-race-save[=slot]` restores a saved career before the race so a test run
gets the upgraded car, credits, power-ups and opponents. It is sufficient on its
own — without an explicit `--quick-race` the race comes from the save.

### Verification results recorded at handoff

All run on the state being handed over (root `7430f40`, BRender `8540bf3`).

| Check | Command | Result |
|---|---|---|
| Build | `cmake --build cmake-build-streamline --parallel 8` | exit 0, `ninja: no work to do` |
| Tests (streamline dir) | `ctest --test-dir cmake-build-streamline` | exit 0, `No tests were found!!!` |
| Tests (tests dir) | `ctest --test-dir cmake-build-tests --output-on-failure` | exit 0, `1/1 test_dethrace Passed`, 100% |
| Plain Vulkan, 75 s | see above | Survived. 7 `VKREND:` lines, **zero validation messages**. |
| DLSS + FG, 95 s | see above | Survived. `frame contract SR=on FG=on Reflex=on PCL=on mask=3`. Scene 960x540, swapchain 1920x1080. |

The DLSS+FG run also **reproduced known issue 1**: a mid-run
`VKREND: swapchain 1918x1080 ... present mode 2` followed by counter samples
falling to 1:1 (`presented=4117 host-presents=2760` overall, 1.49x, with the
final intervals flat). Frame generation was working earlier in the same run.
Treat that log line as the signature of the bug.

### Verification after takeover

| Check | Result |
|---|---|
| Streamline build | exit 0; rebuilt `devpixmp.c`, `vkrend`, and `dethrace.exe` |
| Tests (tests dir) | exit 0; `1/1 test_dethrace Passed` |
| Live resize check, 55 s | initial present mode 1, rebuilt present mode 1; 651 presented / 330 host presents after resize = 1.97x; zero matched errors |

Run the resize check with:

```powershell
.\tools\verify_streamline_resize.ps1 -Repo .
```

## 9. Local inputs not tracked

These are local inputs, not part of the branch, and must not be added:

- `Carma/` — game data, required to run.
- `SDL2-2.32.8/` — local SDL source/package.
- `cmake/zig-toolchain.cmake` — local toolchain file.
- `voxels/` — unrelated user files.

## 10. Current objective and next tasks

**Objective:** keep the Streamline frame contract reliable, then improve the
Vulkan image quality in small, verifiable stages.

Visual stages:

1. Verify and tune the current per-pixel lighting against the car and track.
2. Add a low-cost sky/ground ambient term.
3. Add half-size SSAO before DLSS.
4. Add stable screen-space contact/sun shadows with strict depth guards.
5. Capture clean A/B images and update the visual acceptance notes.

1. **Separate "plug-in primed" from "FG running" (§7.3)** so the counter stops
   producing false alarms and can be trusted in automation.
2. **Repair the debug build** (`dethrace.rc` step) so crashes give symbols.
3. **Recreate the swapchain when FG is toggled**, per the DLSS-G guide §18, and
   prove the simulation still runs at host-frame speed with FG on.
4. **Give FG a correct display-size HUD-less/UI contract (§7.6, §7.7)** and pass
   real camera planes (§7.8).
5. **Decide the sprite-filtering question (§7.2)** — option (c), premultiplied
   alpha, is the only real fix and deserves its own session.
6. **Finish the Stage 4 motion-vector debug view** and validate camera, car and
   2D motion fields.
7. **Capture Stage 5 evidence**: 1080p A/B images, ghosting behind moving cars,
   FPS and latency table.
8. **Re-project world-anchored 2D markers for widescreen (§7.5).**
9. **Add INI/CLI settings**, fallback tests, licence text and shipping docs.

## 11. Files to read first

| File | Why |
|---|---|
| `lib/BRender-v1.3.2/drivers/vkrend/devpixmp.c` | frame targets, acquire/submit/present, Streamline evaluation. Bug §7.1 is at line 307. |
| `lib/BRender-v1.3.2/drivers/vkrend/vkscene.c` | scene targets, deferred retirement, samplers, overlay compositing |
| `lib/BRender-v1.3.2/drivers/vkrend/sbuffer.c` | texture upload, colour key, descriptor sets, sampler choice |
| `lib/BRender-v1.3.2/drivers/vkrend/gstored.c` | stored-model draws, ordered-draw material handling |
| `lib/BRender-v1.3.2/drivers/vkrend/gv1buckt.c` | ordered-draw flush; pairs with `gstored.c` |
| `lib/BRender-v1.3.2/drivers/vkrend/renderer.c` | scene setup, jitter, Streamline camera constants |
| `lib/BRender-v1.3.2/drivers/vkrend/vksetup.c` | instance/device/swapchain creation, feature chain |
| `lib/BRender-v1.3.2/drivers/vkrend/textured_model.frag` | colour key and fog; central to §7.2 |
| `src/harness/streamline_bridge.cpp` | Streamline load, options, tags, Reflex, PCL, telemetry |
| `src/harness/render_config.c` | scene size override and widescreen camera aspect |
| `src/harness/platforms/sdl2.c` | Vulkan surface, Win32 hook, per-frame aspect reassert |
| `src/DETHRACE/common/newgame.c` | `QuickRaceStart`, save-driven quick race |
| `VK-PLAN.md` | staged design and acceptance criteria |
| `docs/RENDERING_PIPELINE.md` | renderer design notes |

A closing note on method: several bugs this session presented as one thing and
were another — the "validation warning" that was actually disabling GPU
features, the sprite bug that was in ordered-draw state rather than textures, the
sprite outlines that were in the source art rather than in filtering. The runs
that settled them were cheap A/B tests (old commit vs new, OpenGL vs Vulkan,
override on vs off). Reach for those before reading more code.
