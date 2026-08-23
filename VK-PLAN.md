# Vulkan renderer for Dethrace, with DLSS 4 + Frame Generation as the end goal

## Context

### One correction first

The premise "everything is rendered in software into one OpenGL texture and then just
displayed" is true for the **default** mode only, not for the whole project.

There are two separate paths today:

| Mode | How it is picked | What it does |
|---|---|---|
| Software (default) | no flag | BRender `softrend` rasterises into `virtualframebuffer`; `src/harness/platforms/sdl2.c:345` uploads that buffer to one SDL texture and blits it |
| Hardware 3D | `--opengl` | BRender `glrend` driver — real GPU pipeline |

`--opengl` is already **real 3D**: `lib/BRender-v1.3.2/drivers/glrend/v1model.c:292`
(`StoredGLRenderGroup`) issues `glDrawElements` per face group from a VAO built out of the
BRender `v11model` in `gstored.c`, with per-scene and per-model UBOs
(`shader_data_scene` / `shader_data_model` in `drivers/glrend/video.h`), hardware lighting,
fog, clip planes and mipmapped textures (`brender.vert.glsl` / `brender.frag.glsl`).

So the job is **not** "add 3D". It is "write a second BRender driver, `vkrend`, that does
what `glrend` does, on Vulkan" — and then build the DLSS-specific extras that `glrend`
cannot host.

The `--opengl` mode is gated on `harness_game_config.opengl_3dfx_mode` plus the presence of
`DATA/PIXELMAP/SMOKE.PIX` (`src/harness/harness.c:236`). The local `Carma/` data dir **has**
that file, so `--opengl` runs on this machine right now. That is the A/B reference for
everything below.

### Where the 2D and the 3D meet (this matters a lot for DLSS)

From `docs/RENDERING_PIPELINE.md` and `drivers/glrend/devpixmp.c`:

1. Game draws 2D background (horizon, map) into `gBack_screen`, an 8-bit paletted buffer.
2. Driver uploads that to a texture and draws it as a fullscreen quad into the FBO.
3. 3D scene renders into the same FBO (`asBack.glFbo` + `asBack.glTex` + depth renderbuffer).
4. Game draws 2D foreground (HUD, messages) into `gBack_screen` again.
5. On `directLock`/`flush` (`devpixmp.c:496-575`), the driver hands the game a CPU buffer
   (`asBack.lockedPixels`), then uploads it into `asBack.overlayTexture` and composites it
   over the FBO with a magenta colour key (`RenderFullScreenTextureToFrameBuffer(...,
   discardPurplePixels=1)`, `devpmglf.c:127`).
6. `doubleBuffer` → `DevicePixelmapGLSwapBuffers` → harness `Swap` → `SDL_GL_SwapWindow`.

**The HUD is already a separate texture with its own colour key.** That maps almost exactly
onto what DLSS-G wants as `kBufferTypeUIColorAndAlpha`, and step 3's FBO maps onto
`kBufferTypeHUDLessColor`. This is the single biggest piece of luck in the whole project.

### 3dfx special effects

`InitLineStuff()` / `InitSmokeStuff()` / `Init2DStuff()` (`src/DETHRACE/common/init.c:605-613`,
under `DETHRACE_3DFX_PATCH` + `opengl_3dfx_mode`) plus the `gVoodoo_rush_mode`,
`gNo_2d_effects`, `gDevious_2d`, `gMaterial_fogging`, `gNo_transients` flags set in
`src/DETHRACE/pc-all/allsys.c:422-443`.

These are **game-level**, not driver-level. They reach the screen through ordinary BRender
DDI calls: the CPU overlay path (step 5 above), `rectangleCopyTo`, `rectangleStretchCopy`,
`rectangleFill`, and normal blended geometry. They need **no special Vulkan work** — they
come for free as soon as `vkrend` implements the same DDI methods `glrend` does. They do
need to be in the parity test set, because they are the easiest thing to silently break.

### BlazingRenderer/BRender

Good call — and better than expected. Checked it directly:

* `master` has no Vulkan driver, but there is a **`driver/vulkan` branch**, head
  `b029d6e` (2026-02-25, "wip"), 11 commits, with a real `drivers/vkrend/` skeleton:
  * `device.c` (21 KB) — instance, physical-device selection by UUID, logical device,
    VMA allocator, queue, command pool
  * `devpixmpf.c` (24 KB) — **front pixelmap backed by a real swapchain**
  * `util.c` — `DeviceVkCreateSwapchain` / `DeviceVkDestroySwapchain` /
    `DeviceVkGetSwapchainFormat`
  * `formats.c` — BRender pixel type → `VkFormat` table
  * `ext_procs.c` — surface create/destroy, resize, window-event hooks
  * `select.c`, `uuid.c`, `outfcty.c`, `rendfcty.c`, glad-for-Vulkan, vendored VMA
  * `sdl3dev` gained "vkrend interop"
* What it does **not** have: `renderer.c`, any `state*.c`, `sstate.c`, `sbuffer.c`,
  `gstored.c`, `gv1model.c`, `gv1buckt.c`, `v1model.c`, any shader. `rendererNew` returns
  `BRE_UNSUPPORTED` (`rendfcty.c:141`).

So it covers the **bootstrap** (roughly the first quarter of the work) and **none** of the
actual rendering.

**Do not rebase Dethrace onto Blazing `master`.** Blazing is BRender **v1.4.0+** and its
`MIGRATION.md` lists breaking changes that Dethrace's decompiled game code depends on
directly: `br_angle` changed from `br_fixed_luf` to `br_scalar`, `BR_MATF_MODULATE` /
`BR_MATF_ZTRANSPARENCY` semantics changed or removed, and **`BR_PMF_KEYED_TRANSPARENCY`,
`br_pixelmap::pm_key`, `pm_copy_function`, `pm_src_key`, `pm_dst_key` deleted outright**.
Dethrace is a decompilation validated against the original binary via `reccmp`; that
migration is a large separate project with its own risk, and it would not move the Vulkan
goal forward by one line.

**Instead: copy the `vkrend` bootstrap files out of `driver/vulkan` and adapt them** to
Dethrace's BRender 1.3.2 DDI and to the existing harness-callback pattern
(`BRT_OPENGL_CALLBACKS_P` → a parallel `BRT_VULKAN_CALLBACKS_P`), rather than to Blazing's
`sdl3dev`. Keep a `docs/` note recording the upstream commit so the two can be diffed later.

### The DLSS end goal — what is actually possible

Verified against Streamline **v2.12.0** (2026-06-23) and NVIDIA DLSS **v310.7.0**:

* **DLSS Super Resolution on Vulkan: supported.** Needs depth, motion vectors,
  render-res input colour, display-res output colour, and jitter applied to the projection
  matrix (`docs/ProgrammingGuideDLSS.md:130-150`).
* **DLSS Frame Generation on Vulkan: supported.** Streamline intercepts
  `vkQueuePresentKHR` and `vkAcquireNextImageKHR`
  (`docs/ProgrammingGuideDLSS_G.md:620`). Device is registered via `slSetVulkanInfo`.
* **Multi Frame Generation (the DLSS 4 "up to 4x" mode) is D3D12-only.**
  `ProgrammingGuideDLSS_G.md:556` — "multi frame generation is not supported ... the
  application is using Vulkan (support is currently limited to D3D12, only)".
  On Vulkan you get classic **2x** FG.
  **Decided: Vulkan, 2x FG accepted.** D3D12 is off the table.
* **VSync + FG is also D3D12-only** (`:1144`). So with FG enabled, VSync must be off.
  The existing `harness_game_config.vsync` key needs to be forced off (with a log line)
  whenever FG is active.
* No Vulkan SDK is installed on this machine (`C:/VulkanSDK` missing, no `glslc`/
  `glslangValidator` in PATH). That is a Stage 1 prerequisite.

The hard part of DLSS here is not the SDK call sequence. It is **motion vectors**. BRender
1.3.2 has no concept of a previous frame and no world space at the driver boundary — the
driver only ever sees `model_to_view` for the current draw. Stage 4 exists entirely to solve
that, and it is the stage most likely to slip.

---

## Plan

Seven stages. Each ends in something you can run and check. Stages 1-3 are the Vulkan port.
Stages 4-6 are DLSS.

**Milestone 1 = Stage 3, and it is a real release.** Vulkan 3D parity ships on its own.
Stages 4-6 are re-scoped only once Stage 3 is running, when the real cost of the DLSS work
is measurable instead of guessed.

---

### Stage 0 — Baseline (half a day)

No code. Establish the thing every later stage is measured against.

1. Build the current tree and run `dethrace --opengl` against `Carma/`.
2. Capture reference screenshots at fixed points: main menu, car select, race start on two
   tracks, rear-view mirror active, map mode, a crash with smoke, a night/fog track.
   Save under `test/reference/` (or a gitignored `staging/`).
3. Record FPS at a fixed resolution.
4. Repeat for software mode, to have both.

**Done when:** a documented, reproducible A/B reference set exists. Everything from here
compares against it.

---

### Stage 1 — `vkrend` boots and clears the screen

**New:** `lib/BRender-v1.3.2/drivers/vkrend/` (submodule — needs its own branch/PR in
`dethrace-labs/BRender-v1.3.2`).

1. Install the Vulkan SDK (validation layers + `glslc`). Add a CMake `find_package(Vulkan)`
   guard so builds without it just skip `vkrend`.
2. Copy from Blazing `driver/vulkan` @ `b029d6e`: `device.c/.h`, `select.c`, `uuid.c/.h`,
   `formats.c`, `util.c`, `outfcty.c/.h`, `rendfcty.c/.h`, `devpixmpf.c`, `driver.c`,
   `drv.h`, `drv_ip.h`, `template.h`, glad-Vulkan, VMA.
3. Adapt to Dethrace's fork:
   * 1.3.2 DDI signatures (`struct br_tv_template` vs `br_tv_template`, `BrFailure`, etc.)
   * Replace the `sdl3dev` interop with a `br_device_vk_callback_procs` struct mirroring
     `br_device_gl_callback_procs`, and a `BRT_VULKAN_CALLBACKS_P` token, following
     `BRT_OPENGL_CALLBACKS_P` in `drivers/glrend/devpmglf.c:46`.
   * VMA is C++. Either add `CXX` to the BRender `project()` languages, or drop VMA and
     hand-roll allocation. **Recommend keeping VMA** — suballocation matters once there are
     thousands of textures, and hand-rolling it is a bug farm.
4. Harness side:
   * `eWindow_type_vulkan` in `src/harness/include/harness/hooks.h`
   * `ePlatform_cap_vulkan` + wiring in `src/harness/harness.c:68-128`
   * `SDL_Vulkan_CreateSurface`, `SDL_Vulkan_GetInstanceExtensions`,
     `SDL_Vulkan_GetDrawableSize`, `SDL_Vulkan_GetVkGetInstanceProcAddr` added to
     `src/harness/platforms/sdl2_syms.h` (and `sdl3_syms.h`)
   * `SDL_WINDOW_VULKAN` branch in `SDL2_Harness_CreateWindow`
     (`src/harness/platforms/sdl2.c:275`)
   * `--vulkan` flag + ini key next to `--opengl` (`harness.c:508`, `:569`)
5. Game side: a third branch in `PDAllocateScreenAndBack`
   (`src/DETHRACE/pc-all/allsys.c:400-460`) calling `BrDevBeginVar(&gScreen, "vkrend", ...)`
   with the same width/height/pixel-type/MSAA tokens. Set the same `gVoodoo_rush_mode` /
   `gInterpolate_textures` / `gMaterial_fogging` block as the `glrend` branch — those flags
   are what turn the 3dfx effect paths on.

**Done when:**
* `dethrace --vulkan` opens a window, creates instance + device + swapchain, presents a
  solid clear colour, and exits cleanly.
* Validation layers report zero errors on start, one frame, and shutdown.
* Builds without the Vulkan SDK still succeed with `vkrend` disabled.

---

### Stage 2 — 2D parity (menus, FMV, HUD)

This is where the game becomes bootable on Vulkan. No 3D yet.

Port, in this order:

| From `glrend` | To `vkrend` | Notes |
|---|---|---|
| `devpixmp.c` `match` | offscreen colour `VkImage` + depth image | replaces FBO + renderbuffer |
| `devpixmp.c:534-575` `directLock`/`directUnlock` | host-visible staging buffer | the CPU overlay buffer |
| `devpixmp.c:496-530` `flush` | staging → `overlayTexture` copy + composite draw | keep the magenta colour key |
| `devpmglf.c:127` `RenderFullScreenTextureToFrameBuffer` | fullscreen-quad pipeline | `default.*.glsl` → SPIR-V |
| `quad.c` | vertex/index buffer + pipeline | |
| `devpixmp.c:290-450` `rectangleCopyTo` / `rectangleCopyFrom` / `rectangleStretchCopy` / `rectangleFill` | `vkCmdCopyBufferToImage` / `vkCmdBlitImage` / `vkCmdClearColorImage` | used by 2D effects |
| `devclut.c` | palette UBO or 256×1 texture | palette animation |
| `devpmglf.c:319` `doubleBuffer` | acquire → record → submit → present | |

Frame structure: **two frames in flight**, per-frame command buffer + fence + acquire/render
semaphores. Use **Vulkan 1.3 dynamic rendering** (`vkCmdBeginRendering`) rather than
`VkRenderPass`/`VkFramebuffer` objects — the state changes per face group in Stage 3 make
static render-pass objects painful, and 1.3 is a safe floor for any GPU that can run DLSS.

Shaders: author as GLSL 450, compile to SPIR-V with `glslc` at build time **and commit the
`.spv` next to the source**, so a Vulkan SDK is not required to build. Mirror the existing
`EmbedResource.cmake` pattern used by `drivers/glrend/CMakeLists.txt`.

**Done when:**
* `dethrace --vulkan` reaches the main menu, menus navigate, the intro SMK plays, palette
  fades work, car-select renders.
* Screenshots of all 2D screens match the Stage 0 `--opengl` references.
* The 3D viewport is black — expected.
* Validation clean over a several-minute session.

---

### Stage 3 — 3D parity

Port the renderer proper. Target: `--vulkan` looks like `--opengl`.

**3a. State and cache.** `state.h`, `state.c`, `state_*.c`, `cache.c`, `sstate.c` are almost
entirely API-agnostic — the token-template plumbing, matrix cache, light/clip-plane packing
into `shader_data_scene`. Copy nearly verbatim, renaming `StateGL*` → `StateVk*`. The only
GL-specific parts are the `GLuint fbo` field in `state_cache` (`state.h:220`) and
`VIDEOI_D3DtoGLProjection`.

**Watch the clip space difference.** GL is z ∈ [-1,1], y-up; Vulkan is z ∈ [0,1], y-down.
Fix it in one place — the projection conversion that today is `VIDEOI_D3DtoGLProjection`
(`drv_ip.h:41`) — not scattered through the shaders. Note that `glrend` already deals with
BRender's upside-down-ness in `renderer.c:102-112` and via `uFlipVertically`; on Vulkan the
y-flip largely cancels out, which is a classic source of "everything is mirrored" bugs.
Verify with an asymmetric test scene, not a symmetric one.

**3b. Textures.** `sbuffer.c` → `VkImage` + `VkImageView` + staged upload + mip generation
via `vkCmdBlitImage`. Keep `br_buffer_stored.blended` and the paletted-source
just-in-time conversion (`v1model.c` `update_paletted_texture`) — the game re-palettes
textures at runtime.

Samplers: `glrend` sets filtering per draw (`v1model.c:246-272`) from
`state->prim.filter` / `mip_filter` / anisotropy. Vulkan samplers are immutable, so build a
**small sampler cache** keyed on (min filter, mag filter, mip mode, max anisotropy) — there
are only about 8 real combinations.

**3c. Pipelines.** `glrend` changes blend / depth-func / depth-write / cull per face group
(`v1model.c:8-100`). In Vulkan:
* Put viewport, scissor and depth bias in **dynamic state**.
* Use `VK_EXT_extended_dynamic_state` (core in 1.3) for cull mode, front face, depth test
  enable, depth write enable and depth compare op — this collapses most of the permutation
  space.
* Keep a **pipeline cache** keyed on the residue: blend mode (the four
  `BRT_BLEND_*` cases in `apply_blend_mode`, `v1model.c:8-44`), colour write mask, and
  primitive topology. That is a handful of pipelines, not thousands.
* Back it with a persisted `VkPipelineCache` on disk to kill first-race hitching.

**3d. Descriptors.**
* set 0 — scene UBO (`shader_data_scene`), bound once per `sceneBegin`
* set 1 — model UBO (`shader_data_model`), **dynamic offset** into a per-frame ring buffer,
  one offset per draw. This replaces `glBufferData` into `uboModel` per group
  (`v1model.c:354`), which would be a correctness bug in Vulkan.
* set 2 — combined image sampler
* `shader_data_scene` / `shader_data_model` are already `std140`-aligned with explicit
  `alignas(16)` and a static assert (`drivers/glrend/video.h:59-122`) — reuse them as-is.

**3e. Geometry.** `gstored.c` → two `VkBuffer`s (positions separate from
UV/normal/colour, as today) + index buffer, uploaded once per `v11model`. `gv1model.c`,
`gv1buckt.c` (the order-table / translucency traversal), `v1model.c`, `onscreen.c` port with
little change — they are traversal logic, not API calls.

**3f. Shaders.** Translate `brender.vert.glsl` (342 lines) and `brender.frag.glsl` (249
lines) to GLSL 450 with explicit `layout(set=, binding=)`. The lighting, fog, clip-plane and
colour-key logic is portable; only the declarations change.

**Done when:**
* Race scenes on `--vulkan` visually match the Stage 0 `--opengl` references. Compare
  screenshots with a tolerance — driver filtering differences mean they will not be
  bit-identical.
* The 3dfx effect set specifically checked: smoke columns, tyre smoke, skid lines, sparks,
  the 2D-over-3D overlay, fog, the rear-view mirror (renders the scene a second time into a
  sub-pixelmap — `allocateSub` must work), map mode.
* Translucency sorts correctly (order table).
* Validation clean through a full race.
* FPS recorded and compared to `--opengl`.

**This is Milestone 1 and it ships.** Tag it, release it, document it in
`docs/RENDERING_PIPELINE.md` and `docs/CHANGELOG.md`. Stages 4-6 get re-scoped from here
with real numbers in hand. If DLSS turns out not to be worth it, the project has still
gained a modern, maintainable renderer.

---

### Stage 4 — DLSS prerequisites

The genuinely hard stage. None of it involves the DLSS SDK yet; all of it is testable on its
own.

**4a. Split render resolution from display resolution.**
Today the offscreen pixelmap is the same size as the screen pixelmap. DLSS needs 3D at a
lower render resolution and the UI composite at display resolution. Change the `vkrend` back
pixelmap to carry its own `render_width`/`render_height`, and move the UI composite and
present to display resolution. Touches `PDAllocateScreenAndBack`
(`src/DETHRACE/pc-all/allsys.c:400`) and the viewport maths in `renderer.c:102-112`.

**4b. Depth as a sampled image.** Change the depth attachment usage to include
`VK_IMAGE_USAGE_SAMPLED_BIT`. Trivial, but it must happen before anything can read it.

**4c. Motion vectors — the real work (wire path implemented).**

Add a second colour attachment (`R16G16_SFLOAT`) written by the main fragment shader as
`currentClipPos.xy/w - previousClipPos.xy/w`, so it needs the previous frame's MVP per draw.

To get that, the driver needs **stable per-instance identity**. It has none today: the same
`br_geometry_stored` is drawn many times per frame (every car, every pedestrian) and the
driver only sees the transform.

The clean fix is a small, contained BRender core change. `renderFaces()` in
`lib/BRender-v1.3.2/core/v1db/modrend.c:19` **already has the `br_actor *actor` argument**
right at the point it dispatches to the driver — it just does not pass it on. Add a
`BRT_IDENTITY_U32`-style token set from the actor pointer immediately before
`GeometryStoredRender` / `GeometryV1ModelRender`, and have `vkrend` key a hash table of
previous-frame MVP matrices on `(actor, geometry, group)`.

Handle these cases explicitly:
* First frame an object is seen → previous = current → zero motion vector.
* Objects that disappear → age out the hash table entries.
* The 2D fullscreen quads (background, HUD) → must **not** write motion vectors.
* Rear-view mirror → a second camera; needs its own MVP history keyed by target pixelmap,
  or DLSS should simply be disabled for the mirror pass.

The current Vulkan path now carries the V1 actor into the driver, keys history by actor,
geometry, group, and target, and writes an `R16G16_SFLOAT` attachment. First-use, stale,
2D, and extra same-frame passes produce zero motion. The shader and image-layout path is
validation-clean at full and split render scales. A visual motion debug view is still open.

**4d. Jitter (implemented).** Apply a Halton(2,3) sub-pixel offset to the projection matrix. Inject it in
exactly one place — where the GL/Vulkan projection is derived in the state cache — so the
un-jittered matrix stays available to hand to Streamline (`sl::Constants` matrices must be
jitter-free, `ProgrammingGuideDLSS.md:206`). The Vulkan path uses an 8-sample sequence;
rendering uses the jittered projection while motion and future Streamline matrices use the
unjittered projection.

**4e. Expose the layers.** Formalise what already exists: HUD-less colour = the 3D target
after the scene and before the overlay composite; UI colour+alpha = `asBack.overlayTexture`
with the magenta key converted to real alpha.

**Done when:**
* A debug view (bindable key) renders the motion-vector buffer as colour.
* Panning the camera over static geometry gives a smooth, coherent field.
* A car moving across a static camera shows the car's vectors clearly distinct from the
  background.
* 2D layers show zero motion.
* The game still renders correctly at render resolution ≠ display resolution.

---

### Stage 5 — DLSS Super Resolution

1. Integrate Streamline v2.12.0. Vendor `sl.interposer` + `sl.common` + `sl.dlss`, plus
   `nvngx_dlss.dll` from NVIDIA/DLSS v310.7.0. Both are redistributable under NVIDIA's
   licence — read it and add the required attribution before shipping binaries.
2. `slInit` → `slSetVulkanInfo` → `slIsFeatureSupported(sl::kFeatureDLSS)`, with a clean
   fallback to plain Vulkan when unsupported (no NVIDIA GPU, old driver, AMD/Intel).
3. Per frame: `slSetConstants` (jitter-free view/proj, jitter offsets in pixel space,
   `mvecScale = {1/renderWidth, 1/renderHeight}` for pixel-space vectors), tag
   `kBufferTypeScalingInputColor`, `kBufferTypeScalingOutputColor`, `kBufferTypeDepth`,
   `kBufferTypeMvec`, then `slEvaluateFeature`.
4. Config: `dlss=off|dlaa|quality|balanced|performance` in the ini and CLI, alongside the
   existing `msaa_samples` / `vsync` / `anisotropy_limit` keys in
   `src/harness/include/harness/config.h`.

**Done when:** DLSS Quality at 1080p output is sharper than 1080p native with MSAA off, has
no visible ghosting behind moving cars, and shows a measured FPS gain. Ghosting is the
signal that Stage 4c is wrong — if it appears, go back, do not tune around it.

---

### Stage 6 — DLSS Frame Generation

1. Enable `sl.dlss_g`. Streamline takes over `vkQueuePresentKHR` and
   `vkAcquireNextImageKHR`; the present path from Stage 2 must go through the interposer
   rather than calling Vulkan directly.
2. Tag `kBufferTypeHUDLessColor` and `kBufferTypeUIColorAndAlpha` from Stage 4e. Without
   these the HUD will warp and shimmer on generated frames.
3. Integrate Reflex/PCL for frame pacing — DLSS-G effectively requires it.
4. The game logic is frame-rate coupled (`harness_game_config.fps`, `physics_per_frame`).
   Confirm that doubling *presented* frames while *rendered* frames stay constant does not
   disturb the physics or the input path. This is a real risk in a 1997 engine and deserves
   its own test pass.

**Done when:** FG on doubles presented FPS, the HUD stays stable, input latency is measured
(not guessed) and stays acceptable, and toggling FG at runtime does not crash.

**Expect 2x, not 4x** — Multi Frame Generation is D3D12-only in Streamline 2.12. Accepted.
Also force `vsync=0` while FG is on, since VSync+FG is D3D12-only too.

---

## Files touched (summary)

**New:** `lib/BRender-v1.3.2/drivers/vkrend/*` (about 30 files, the bulk of the work);
`lib/BRender-v1.3.2/drivers/vkrend/*.glsl` + committed `.spv`.

**Modified, BRender submodule:**
* `drivers/CMakeLists.txt`, root `CMakeLists.txt` — add `vkrend`, optional `CXX` for VMA
* `core/v1db/modrend.c` — pass actor identity (Stage 4c only)
* `core/inc/` token header — `BRT_VULKAN_CALLBACKS_P`, identity token

**Modified, Dethrace:**
* `src/harness/include/harness/hooks.h` — `eWindow_type_vulkan`, `ePlatform_cap_vulkan`
* `src/harness/include/harness/config.h` — `vulkan_mode`, `dlss_mode`, `frame_gen`
* `src/harness/harness.c` — flag parsing, ini keys, platform capability match
* `src/harness/platforms/sdl2.c` + `sdl2_syms.h` — Vulkan window + surface (same for sdl3)
* `src/DETHRACE/pc-all/allsys.c:400` — `BrDevBeginVar(&gScreen, "vkrend", ...)` branch
* `docs/RENDERING_PIPELINE.md` — document the Vulkan path next to the OpenGL one

---

## Verification

Per stage, as listed above. Across the whole project:

* **Validation layers on in every debug build**, treated as build-breaking. This is the
  single highest-value habit in a Vulkan port.
* **RenderDoc captures** at each stage, kept for comparison.
* **Screenshot A/B against Stage 0 references** with a tolerance-based diff, run at every
  stage from 2 onward. The 3dfx effect scenes (smoke, skids, sparks, fog, mirror, map) are
  the highest-value cases because they exercise the paths most likely to break silently.
* `cmake --build` with `-DDETHRACE_WERROR=ON` and the existing `test/` suite (`BUILD_TESTS=ON`)
  green — `test/` includes `eb14b55 test(graphics): lock legacy display defaults`, which
  guards existing display behaviour.
* Non-NVIDIA and no-Vulkan-SDK machines must still build and run; `--opengl` and software
  mode must remain untouched throughout.

---

## Honest risk assessment

| Risk | Severity | Note |
|---|---|---|
| Motion vectors (Stage 4c) | **High** | The core change is small, but per-instance identity in a 1997 scene graph is where this project most likely stalls. Nothing about DLSS quality works without it. |
| Blazing `vkrend` is WIP and 177 commits behind | Medium | It is a skeleton, not a product. Budget real time for adapting it to the 1.3.2 DDI. |
| VMA needs C++ in a C-only project | Low | Add `CXX` to `project()`. Alternative is worse. |
| Frame-rate-coupled game logic vs FG | Medium | Old engine; needs its own test pass at Stage 6. |
| Scope | **High** | Stages 1-3 alone are a substantial port (~6500 lines of `glrend` equivalent). Stages 4-6 are comparable again. Mitigated by shipping Stage 3. |

Resolved, no longer risks: backend choice (Vulkan, 2x FG accepted) and milestone structure
(Stage 3 ships).

## Decisions on record

* **Backend: Vulkan.** 2x Frame Generation accepted; 4x MFG and VSync+FG (both D3D12-only)
  are out of scope.
* **Milestone 1 is Stage 3** — Vulkan 3D parity, released and tagged on its own.
  Stages 4-6 are re-scoped after it, not committed to up front.
* **No rebase onto BlazingRenderer `master`.** Seed `vkrend` from the `driver/vulkan`
  branch instead, adapted to the 1.3.2 DDI.

## Next step

Stage 0: build the current tree, run `dethrace --opengl` against `Carma/`, and capture the
reference screenshot set. Everything after that is measured against it.
