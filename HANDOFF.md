# Handoff — Vulkan renderer port

Branch: `feature/vulkan-renderer` (worktree at `C:\Users\xr1p\CLionProjects\dethrace-vulkan`)
Last commit at handoff: see `git log -1`
Date: 2026-08-23

---

## 1. Project purpose and target state

Dethrace is a decompilation of Carmageddon (1997) rebuilt to run natively. It renders through
BRender 1.3.2, vendored as a submodule at `lib/BRender-v1.3.2` (fork:
`dethrace-labs/BRender-v1.3.2`).

**Target state for this line of work:** a Vulkan renderer capable of hosting NVIDIA DLSS 4.x
Super Resolution and Frame Generation.

The route is a new BRender driver, `vkrend`, sitting alongside the existing `glrend`, reached
via a `--vulkan` flag. The full plan (7 stages, acceptance criteria per stage) is in `VK-PLAN.md` at the repo
root.

### Renderers that exist

| Mode | Flag | Driver | State |
|---|---|---|---|
| OpenGL | `--opengl` | `glrend` | Full 3D. The reference implementation and the A/B baseline. |
| Software | `--software` | `softrend` + `virtualframebuffer` | Full 3D on CPU. |
| **Vulkan** | `--vulkan` | **`vkrend`** | **Stage 1: device + swapchain + present. No 3D yet.** |

Note `opengl_3dfx_mode` defaults to `1` in this tree, so omitting `--opengl` does *not*
select the software renderer — that is what `--software` is for.

### DLSS constraints (verified against Streamline v2.12.0 / NVIDIA DLSS v310.7.0)

* Streamline requires **Vulkan 1.2 or higher**. We request 1.3. A newer API version is not a
  problem — 1.2 is a floor, not a ceiling.
* DLSS Super Resolution **and** Frame Generation both work on Vulkan; SL intercepts
  `vkQueuePresentKHR` and `vkAcquireNextImageKHR`.
* **Multi Frame Generation (the DLSS 4 "4x" mode) is D3D12-only.** Vulkan gets classic 2x FG.
  The user has accepted this; D3D12 is out of scope.
* VSync + FG is also D3D12-only, so `vsync` must be forced off when FG is enabled.
* Integration works by loading `sl.interposer.dll` **instead of** `vulkan-1.dll` and taking
  `vkGetInstanceProcAddr` from it. **This is the single most important design constraint in
  the driver** — see §4.

---

## 2. What was implemented in this session

Four commits, oldest first.

### `ae2349e` — decrypted plaintext `.TXT` game data

**Problem addressed:** game data was unreadable, which makes debugging race/material/car
definitions during a renderer port far harder than it needs to be.

**Finding:** the common assumption that Carmageddon's assets are "packed or encrypted" is
mostly wrong. Across all 3418 files: there are **no archives**, and **only `.TXT` is
encrypted** (205 of 231 files, 11.6 MB). `.PIX` (1158), `.DAT` (337), `.ACT` (391), `.MAT`
(320), `.FLI` (642), `.WAV` (185), `.TAB` (62), `.SMK` (10) are already plain, engine-native
or standard formats.

The cipher is per *line*: a line starting with `@` has the remainder scrambled by a 16-byte
XOR (`EncodeLine()`, `src/DETHRACE/common/utility.c:93`).

**Changes:**
* `tools/decrypt_game_data.py` — batch driver that **reuses the codec already in the repo**
  (`tools/decode_datatxt.py`) rather than reimplementing the cipher. Verifies every file by
  re-encrypting the result and comparing byte-for-byte before writing.
* `src/DETHRACE/common/loading.c:3500` — relaxed the first-byte `@` gate in `OldDRfopen()`,
  guarded by `DETHRACE_FIX_BUGS` so reccmp matching builds keep original behaviour. Nothing
  else was needed: `GetALineWithNoPossibleService()` already passes non-`@` lines through
  untouched.

**Verified:** 205/205 decrypted, 0 verification failures, 205/205 exact round-trip, zero
`@`-prefixed lines left in 792,009 lines (so no re-decode hazard).

> **Gotcha for the next person:** `EncodeLine()` is *not* a clean involution. It transforms in
> place, so the `//`-comment key switch observes plaintext when decoding but ciphertext when
> encoding. A true inverse encoder must test the *input* bytes. The repo's
> `tools/decode_datatxt.py` gets this right.

### `c4e998d` — `--quick-race` and a reference screenshot harness

**Problem addressed:** stages 1–3 need a reproducible baseline of what the current renderers
produce, which means reaching actual gameplay deterministically. Driving the front-end with
synthetic keystrokes proved unreliable, so the front-end is skipped instead.

**Changes:**
* `--quick-race[=N]` — loads race N directly, live race in ~18 s, no menu input.
  `QuickRaceStart()` (`src/DETHRACE/common/newgame.c`) mirrors the tail of `DoNewGame()`;
  `DoGame()` takes the same non-interactive race-info load path networked play already uses,
  bypassing `DoSelectRace()`, `DoNewGameAnimation()` and `DoGridPosition()`.
* `--skill=N`, and `--software` as a counterpart to `--opengl`.
* `tools/capture_reference.ps1` + `tools/GameInput.ps1` — captures chase view, cockpit,
  rear-view mirror and map mode.

All game-side insertions sit behind `DETHRACE_FIX_BUGS`, so reccmp matching builds keep the
original control flow.

> **Non-obvious:** `DoSelectRace()` does not just draw a screen — it also loads the race info
> (`racestrt.c:1784`). Skipping it blindly leaves the race unloaded. The quick path reuses the
> net branch's `LoadRaceInfo`/`FillInRaceInfo`/`DisposeRaceInfo`.

### `1c79df6` + submodule `46177e4` — the Vulkan driver and `--vulkan` mode

This is the substance of the session. See §3 and §4.

### `f894856` — merge of `main`

Brought in 5 commits from `main`, notably `434d3d1 fix(graphics): render horizon sky in
OpenGL mode`, plus sparks and view-distance work. **These key off
`harness_game_config.opengl_3dfx_mode`, and `--vulkan` sets that flag too, so the horizon/sky
and effects work applies to the Vulkan path automatically.**

---

## 3. What currently works

Verified on an **NVIDIA GeForce RTX 5060 Ti**, driver-reported Vulkan **1.4.341**:

```
VKREND: device   = NVIDIA GeForce RTX 5060 Ti
VKREND: api      = 1.4.341
VKREND: swapchain 800x600, 3 images, format 44, present mode 2
```

* `dethrace --vulkan` opens a Vulkan window, creates instance → physical device → logical
  device → 3-image swapchain (`VK_FORMAT_B8G8R8A8_UNORM`, FIFO).
* The game boots and runs on the Vulkan path without crashing, including with `--quick-race`.
* Frames present: the window shows the clear colour set in `doubleBuffer`
  (RGB 0.10/0.25/0.45) — proving acquire → record → clear → barrier → submit → present is
  driven end-to-end by the game's own frame loop.
* Clean shutdown: `WM_CLOSE` → **exit code 0**, no Vulkan errors logged.
* `--opengl` and `--software` are unaffected (re-checked after all changes; both render
  distinct, non-blank frames).
* Builds with **no Vulkan SDK installed** — glad is vendored and self-contained, and nothing
  links against a Vulkan library.

Stage 0 reference sets exist at `staging/reference/{opengl,software}/` (9 frames each,
gitignored, ~36 MB, not committed).

---

## 4. Architecture and design decisions

### Why `vkrend` mirrors `glrend` and not BlazingRenderer's WIP driver

BlazingRenderer/BRender has a `driver/vulkan` branch (head `b029d6e`, 2026-02-25) with a
`vkrend` skeleton: instance/device selection, VMA, swapchain, format tables. It has **no**
renderer, state machinery, geometry or shaders, and `rendererNew` returns `BRE_UNSUPPORTED`.

It is written against BRender **1.4.0+**, whose `MIGRATION.md` deletes
`BR_PMF_KEYED_TRANSPARENCY`, `pm_key`, `pm_copy_function`, and changes `br_angle` to
`br_scalar` — all of which the decompiled game code and reccmp validation depend on.

**Decision: do not rebase onto Blazing master.** `vkrend` is structured to mirror
dethrace's own `glrend`, which guarantees a 1.3.2 DDI fit. Only glad was taken from that
branch.

### The single-loader rule (this is the DLSS seam — do not break it)

glad is generated in **MX mode**: every Vulkan entry point lives in a `GladVulkanContext`,
filled from one `vkGetInstanceProcAddr` supplied by the harness via
`SDL_Vulkan_GetVkGetInstanceProcAddr`.

That is deliberate. Streamline is integrated by loading `sl.interposer.dll` in place of
`vulkan-1.dll` and taking `vkGetInstanceProcAddr` from it. Because all dispatch flows through
that one pointer, **enabling DLSS later changes the harness and touches no call site in the
driver.** Do not introduce direct `vk*` calls or a second loader.

### Layering

```
src/DETHRACE/pc-all/allsys.c   PDAllocateScreenAndBack() picks vkrend / glrend / virtualfb
        │  BrDevBeginVar(&gScreen, "vkrend", ..., BRT_VULKAN_CALLBACKS_P, &vk_callbacks, ...)
        ▼
lib/BRender-v1.3.2/drivers/vkrend/
    driver.c     BrDrv1VKBegin — creates the BRT_VULKAN_CALLBACKS_P token
    device.c     br_device, output/renderer facilities, token matching
    outfcty.c    output facility → pixelmapNew
    devpixmp.c   screen pixelmap (owns br_vk_state); doubleBuffer = acquire/clear/present
    vksetup.c    instance, physical device, logical device, swapchain, frame sync
    rendfcty.c   renderer facility (+ null geometry objects)
    renderer.c   no-op renderer (stage 3 fills this in)
    gv1model.c   no-op V1Model geometry format
    glad/        vendored, self-contained Vulkan 1.4 loader (MX mode)
        ▲
        │  br_device_vk_callback_procs (void* handles — core stays free of Vulkan headers)
src/harness/platforms/sdl2.c   window, surface, instance extensions, drawable size
```

### Three traps that cost real time (all commented in place)

1. **Global commands must be resolved with a NULL instance.** `vkEnumerateInstance*` and
   `vkCreateInstance` return NULL from the Windows loader once a real instance is passed.
   Getting this wrong makes glad's post-instance reload fail, because it cannot enumerate
   extensions. See `vkrend_is_global_command()` in `vksetup.c`.
2. **The callbacks token must be in `insignificantMatchTokens`**, or `ObjectContainerFind()`
   rejects the output facility and `BrDevBeginTV()` bails out *before ever calling*
   `pixelmapNew`. glrend lists `BRT_OPENGL_CALLBACKS_P` for the same reason; ours is created
   at runtime, so `DeviceVkAllocate()` fills the slot.
3. **The token is created at runtime** via `BrTokenCreate()` and matched by name through the
   `DEV()` template macro, because `core/inc/pretok.{h,c}` and `toktype.c` are generated by a
   Perl script that is not available in this environment.

### Other decisions

* **Vulkan 1.3 requested**, with `dynamicRendering` and `synchronization2` enabled — ahead of
  the later renderer stages, which use dynamic rendering instead of `VkRenderPass` objects.
* **Two frames in flight** (`VKREND_FRAMES_IN_FLIGHT`) — enough to keep the GPU fed without
  adding latency that would work against frame generation.
* **Swapchain images get `TRANSFER_DST`** usage so stage 2 can blit the rendered target
  straight in.
* **Offscreen pixelmaps use BRender's stock `br_device_pixelmap_mem` match**, as `virtual_fb`
  does. This is what lets the game boot; stage 2 replaces it with Vulkan-backed targets.

---

## 5. What is incomplete or broken

### The Vulkan renderer draws nothing

`renderer.c` accepts every call and draws nothing; `gv1model.c` accepts models and draws
nothing; `rendererNew` succeeds but produces a renderer with no pipeline. **The window shows
a flat clear colour, not the game.** This is expected at stage 1 and is the entire content of
stages 2–3.

Two deliberate compromises exist so the game can boot at all, and both should be revisited:

* `renderer.c` returns `BRE_OK` from state calls it ignores, because the game treats a failed
  state call as fatal. Once the real state machinery lands, these must return real results.
* `gv1model.c`'s `storedNew` returns `BRE_FAIL`, so the v1db falls back to immediate mode,
  where `render` silently accepts. This is a stub, not a design.

### Validation layers were never exercised

No Vulkan SDK is installed on this machine (`C:/VulkanSDK` absent, no `glslc` in PATH). The
driver requests `VK_LAYER_KHRONOS_validation`, logs
`validation layers requested but not available, continuing without`, and proceeds.

**This is the one stage-1 acceptance criterion that could not be verified.** Installing the
SDK is the first thing the next person should do — a Vulkan port developed without validation
accumulates silent corruption. The driver already picks the layers up automatically at
runtime once present.

### Known issues not caused by this work

* `test_loading_GetCDPathFromPathsTxtFile` **fails**, and aborts the test run so later tests
  do not execute. Pre-existing: verified by running the same binary against both the
  decrypted assets and the untouched originals — identical failure. Cause is this install's
  `PATHS.TXT` holding a real installer path (`C:\DOCUME~1\...`) while the test expects
  `.\DATA\MINICD`.
* libsmacker logs `smk_open_filepointer ... returning NULL` at boot on every renderer,
  including `--opengl`. Pre-existing, unrelated.
* Stage 0 reference set is single-track (race 0 only). `-Race N` is wired but unused.

### Technical debt / risks

* The BRender submodule pin `6201593` ("feat(glrend): add anisotropy limit hook") exists
  **only locally, on no remote**. `git submodule update --init` in a fresh worktree fails with
  *"not our ref"*. It now has a named branch (`feature/vulkan-renderer`) in this worktree so a
  stray `gc` cannot orphan it, **but it still needs pushing somewhere.** Same now applies to
  the new `46177e4`.
* `vkrend` has no resize handling. `VK_ERROR_OUT_OF_DATE_KHR` currently skips the frame;
  stage 2 must rebuild the swapchain.
* Motion vectors (plan stage 4c) remain the highest-risk item for DLSS. The useful finding:
  `core/v1db/modrend.c:19` `renderFaces()` already has `br_actor *actor` in scope exactly
  where geometry is dispatched to the driver — it simply does not forward it. That makes
  per-instance identity a small, contained core change rather than a scene-graph rewrite.

---

## 6. How to build, run, test

### Build

The RC compiler must be set at first configure, or ninja bakes in a bare `windres` that is
not on PATH.

```
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER="C:/Users/xr1p/AppData/Local/Programs/CLion 2/bin/mingw/bin/gcc.exe" ^
  -DCMAKE_RC_COMPILER="C:/Users/xr1p/AppData/Local/Programs/CLion 2/bin/mingw/bin/windres.exe" ^
  -DCMAKE_MAKE_PROGRAM="C:/Users/xr1p/AppData/Local/Programs/CLion 2/bin/ninja/win/x64/ninja.exe" ^
  -DCMAKE_PREFIX_PATH="<repo>/SDL2-2.32.8"
cmake --build cmake-build-release
```

Copy `SDL2-2.32.8/x86_64-w64-mingw32/bin/SDL2.dll` next to the exe (the project does not).

### Run

Set `DETHRACE_ROOT_DIR=<repo>/Carma` — the exe resolves data next to itself, not from cwd.

```
dethrace --vulkan  --quick-race --window --window-width=800 --window-height=600
dethrace --opengl  --quick-race --window ...
dethrace --software --quick-race --window ...
```

### Test

```
cmake -S . -B cmake-build-tests -G Ninja -DBUILD_TESTS=ON <same toolchain args>
cmake --build cmake-build-tests
cmake-build-tests/dethrace_test.exe
```

### Screenshots

```
powershell -ExecutionPolicy Bypass -File tools/capture_reference.ps1 `
  -Tag opengl -GameArgs '--opengl' -Repo <repo>
```

Three things in that harness are load-bearing and documented in `tools/GameInput.ps1`:

* **`SendKeys` does not work.** SDL reads raw scancodes, and Windows blocks
  `SetForegroundWindow` from a background process, so presses land in the calling terminal.
  Input goes through `SendInput` with `KEYEVENTF_SCANCODE` plus an `AttachThreadInput`
  foreground handoff.
* **Driving is the numeric keypad, not the arrows.** `kp8` accelerates. Arrow Up and numpad 8
  share scancode `0x48` and differ only by the extended flag; arrows leave the car in neutral.
* **`SetProcessDPIAware()` before any GDI use.** This display is 3840×2160 at 150%; without
  it `GetClientRect`/`CopyFromScreen` disagree by 1.5× and every capture is offset.
* Capture **windowed** — fullscreen captures come back black.

### Exact results at handoff

| Check | Result |
|---|---|
| Release build | **pass**, exit 0 |
| Test build (`BUILD_TESTS=ON`) | **pass**, exit 0, 317/317 targets |
| Test suite | **8 passed, 1 failed**, exit 15 — `test_loading_GetCDPathFromPathsTxtFile` (pre-existing, see §5) |
| `--vulkan` runtime | instance + device + 3-image swapchain, presents clear colour, `WM_CLOSE` → exit 0 |
| `--opengl` runtime | renders normally, non-blank |
| `--software` runtime | renders normally, non-blank |

---

## 7. Current objective and next steps

**Current objective:** stage 1 (Vulkan bring-up) is complete. The next milestone is **stage 2:
2D parity** — get the game's actual output on screen through Vulkan.

Ordered by priority:

1. **Install the Vulkan SDK and re-run everything with validation enabled.** Do this before
   writing more Vulkan code. Fix whatever it reports.
2. **Push the BRender submodule commits** (`6201593`, `46177e4`) to a remote. They exist only
   on this machine.
3. **Stage 2 — 2D parity.** Replace the clear in `devpixmp.c`'s `doubleBuffer` with a real
   upload of the game's back buffer:
   * host-visible staging buffer + `vkCmdCopyBufferToImage` into the swapchain image;
   * swapchain rebuild on `VK_ERROR_OUT_OF_DATE_KHR` / window resize;
   * then port the offscreen pixelmap properly (`match`, `directLock`/`directUnlock`/`flush`,
     the magenta-keyed overlay composite, `rectangleCopyTo`/`StretchCopy`/`Fill`, `devclut`).
   * **Done when:** menus, intro FMV, palette fades and the HUD match the stage 0 OpenGL
     reference set. The 3D viewport may still be black.
4. **Stage 3 — 3D parity.** Port state machinery (`state*.c`, `cache.c`, `sstate.c` — largely
   API-agnostic), textures (`sbuffer.c` → `VkImage`), pipelines (dynamic state +
   `VK_EXT_extended_dynamic_state`, small pipeline cache), descriptors (set0 scene, set1 model
   via dynamic offset, set2 texture), geometry (`gstored.c`), and translate
   `brender.vert/frag.glsl` to GLSL 450 → SPIR-V.
   * Watch the clip-space difference: GL is z ∈ [-1,1] y-up, Vulkan z ∈ [0,1] y-down. Fix it
     in **one** place (the projection conversion), not in the shaders. Verify with an
     asymmetric scene — a symmetric one hides mirroring bugs.
   * **Acceptance explicitly includes skyboxes and sunsets.** The software renderer draws a
     sunset horizon that `glrend` historically did not; `main` commit `434d3d1` addresses this
     for OpenGL, and it applies to Vulkan via `opengl_3dfx_mode`. Matching `glrend` alone is
     not sufficient — check against the software reference too.
   * **Stage 3 is a shippable release** in its own right. Tag it.
5. **Stages 4–6 — DLSS.** Re-scope only once stage 3 runs. Stage 4 (split render/display
   resolution, depth as sampled image, motion vectors, jitter) is the hard part; stages 5–6
   are the SDK integration. Expect **2x** FG, not 4x.

---

## 8. Files worth reading first

| Path | Why |
|---|---|
| `VK-PLAN.md` | Full 7-stage plan with per-stage acceptance criteria and risk table |
| `lib/BRender-v1.3.2/drivers/vkrend/vksetup.c` | All Vulkan bring-up; read `vkrend_load()` first |
| `lib/BRender-v1.3.2/drivers/vkrend/devpixmp.c` | Screen pixelmap and the present loop — where stage 2 starts |
| `lib/BRender-v1.3.2/drivers/vkrend/device.c` | Token matching, incl. the `insignificantMatchTokens` trap |
| `lib/BRender-v1.3.2/drivers/glrend/` | **The reference implementation.** Every stage 2/3 task has a counterpart here |
| `lib/BRender-v1.3.2/drivers/glrend/v1model.c:292` | `StoredGLRenderGroup` — the actual GL draw call to mirror |
| `lib/BRender-v1.3.2/drivers/glrend/video.h:59-122` | `shader_data_scene` / `shader_data_model`, already std140-aligned; reuse as-is |
| `src/DETHRACE/pc-all/allsys.c:400` | `PDAllocateScreenAndBack()` — renderer selection |
| `src/harness/platforms/sdl2.c` | Vulkan window/surface, and the loader seam for DLSS |
| `docs/RENDERING_PIPELINE.md` | How 2D and 3D layers combine — essential background for stage 2 |
| `core/v1db/modrend.c:19` | `renderFaces()` — has the `br_actor*` that stage 4c needs |
| `tools/GameInput.ps1` | Input/capture traps (scancodes, keypad, DPI) |

---

## 9. Repository state

Committed on `feature/vulkan-renderer`; working tree clean apart from the ignored paths below.

Intentionally **not** committed (all gitignored):

| Path | Size | Why |
|---|---|---|
| `Carma/` | 202 MB | Game assets. `.TXT` files here are **decrypted in place** — this copy is not pristine. Original encrypted assets remain in the main checkout at `C:\Users\xr1p\CLionProjects\dethrace\Carma`. |
| `SDL2-2.32.8/` | 89 MB | Local MinGW SDL2 devel zip |
| `cmake-build-release/`, `cmake-build-tests/` | 38 MB | Build outputs |
| `staging/` | 36 MB | Stage 0 reference screenshots and scratch captures |
| `tools/__pycache__/` | 16 KB | Python cache |
| `cmake/zig-toolchain.cmake` | — | Pre-existing local toolchain file, untracked on `main` too |

`.gitignore` was extended this session with `/cmake-build*`, `/staging` and `__pycache__/` to
stop those being committed by accident.
