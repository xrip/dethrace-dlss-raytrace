# Quality-of-life work plan

State: draft for user approval. No engine code has been changed.

## Main rules

- One change per commit.
- Put the test and the short user note in the same commit as the change.
- Keep all old defaults until the user gives approval for a new default.
- Keep software rendering the same. New picture-quality controls first go to OpenGL.
- Keep new platform code in `src/harness`. Make only small hooks in `src/DETHRACE`.
- Do not make a general LOD system before there are models for it.
- Do not put game data, PS One data, or a font with an unclear license in Git.
- Stop a stage when its check is bad. Do not hide the bad result with a guard.

## What is in the engine now

1. Draw distance is already present. `Yon` is kept in `OPTIONS.TXT`, and the game menu has four values from 20 to 35. `SetYon()` changes all game cameras.
2. Car LOD is already present. Cars may have more than one model, and the engine changes the model by camera distance. The game menu already changes `CarSimplificationLevel`.
3. OpenGL already draws the 3D view at the window viewport size. The 2D game picture and HUD still use the old 320x200 or 640x480 surface.
4. BRender already has an `MSAA_SAMPLES` value, but Dethrace does not pass a value to it. The SDL OpenGL window also does not ask for a multisample buffer.
5. Anisotropic filtering is already on at the GPU's highest value when a material has mipmaps and linear filtering. There is no user limit.
6. There are two game font paths: `tDR_font` uses `.PIX` plus `.TXT`; `br_font` uses old `.FNT` files. A full font change has to cover both.
7. The model path reads BRender `.DAT` and `.ACT`. There is no PS One model reader or conversion tool.

These points come from [options.c](../src/DETHRACE/common/options.c), [loading.c](../src/DETHRACE/common/loading.c), [car.c](../src/DETHRACE/common/car.c), [allsys.c](../src/DETHRACE/pc-all/allsys.c), [sdl2.c](../src/harness/platforms/sdl2.c), [v1model.c](../lib/BRender-v1.3.2/drivers/glrend/v1model.c), and [the rendering note](RENDERING_PIPELINE.md).

## Scope used by this draft

If work has to run without a new answer from the user, use these choices:

- OpenGL with SDL2 and SDL3 on all current PC systems.
- The software path stays pixel-for-pixel the same.
- High-resolution 3D stays 4:3 first. True widescreen is a later, separate change.
- New start-time video values go in `dethrace.ini`.
- Live old game values, such as `Yon`, stay in `OPTIONS.TXT`.
- TrueType is optional at first. The old font path stays as the safe setting.
- PS One work is an offline tool. The engine will keep loading BRender files.

## Questions that need an answer before code changes

1. Is the target a Windows-only fork, or code fit for upstream on Windows, Linux, and macOS? The draft uses the upstream choice.
2. Does “native resolution” mean high-resolution 4:3 3D, or full 16:9 with moved HUD parts? The draft does 4:3 first.
3. Should TrueType cover all menu and HUD text, or HUD text first? The draft first keeps the old text API and covers both font types.
4. Which PS One disc version and which models are wanted? A path to an owned disc image or an unpacked copy is needed before that stage.
5. Which TrueType files are approved? They need a clear OFL, Apache-2.0, MIT, or public-domain license.

## Stage 0 - lock the old result

### Commit 1: `docs(qol): add quality work checks`

Change:

- Add this plan and the test matrix below.
- Record the old default values and the files that own them.

Check:

- No binary or game-data file is in the commit.
- No run-time result changes.

### Commit 2: `test(graphics): lock original quality defaults`

Change:

- Add small tests for `Yon` save/load, car detail save/load, and default harness video values.
- Add pure value tests for MSAA and anisotropy input limits before those values reach SDL or OpenGL.

Check:

- No-data tests pass.
- Full tests pass when `DETHRACE_ROOT_DIR` points to an owned Splat Pack copy.
- Old saves and an old `dethrace.ini` still work.

## Stage 1 - draw distance and car detail

### Commit 3: `feat(graphics): extend view-distance control`

Change:

- Reuse `GetYon()`, `SetYon()`, `AssertYons()`, and the present `OPTIONS.TXT` key.
- Use a small table for four clear presets. Test values 35, 70, 140, and 280 first; keep 35 as the default.
- Apply the value to front, rear, replay, and special cameras through the one present setter.
- Keep AI wake distance at the old game value. A picture setting must not change race logic.
- Keep fog, sky, and track culling tied to the same camera value.

Check:

- The selected value comes back after a new run.
- All cameras use it.
- AI car state is the same at 35 and at a higher picture distance.
- No missing track parts, sky gap, or bad fog in at least three races.
- Frame time and visible actor count are written down for each preset.

Stop rule:

- If a value above 35 gives depth fighting, do not add a depth-format change here. Use the later conditional depth commit.

### Commit 4: `feat(graphics): add car LOD distance control`

Change:

- Reuse the present car model list, `min_distance_squared`, and `SwitchCarActor()`.
- Keep the old `CarSimplificationLevel` data key for old saves.
- Give the four menu points clear distance meaning: `Original`, `Far`, `Very far`, and `Always high`.
- Put the scale in one table. Do not add a second LOD tree or make new meshes.
- Keep the local player's car rule the same.

Check:

- A test car changes to the expected model at fixed camera distances.
- Moving back and forward over a limit gives a stable result.
- Network and replay runs do not change car state or damage state.
- The value is saved and read back.

## Stage 2 - output size and texture quality

### Commit 5: `feat(video): use the OpenGL drawable size`

Change:

- Add `WindowWidth` and `WindowHeight` to `dethrace.ini`; zero keeps the current result.
- For OpenGL, use the real drawable pixel size for `glViewport`, not only the logical SDL window size.
- Make the first viewport at window creation and again on resize.
- Keep the old 640x480 game surface for 2D art. Do not add a third `gGraf_specs` entry.

Why:

- The 3D path can already draw at the physical viewport size. A new fixed 1920x1080 game mode would spread fixed-size UI trouble through old code.

Check:

- 640x480, 1280x960, 1920x1080, and one high-DPI screen.
- On 1920x1080, the first result is a centered 1440x1080 4:3 view with black sides.
- Mouse input, rear view, map, FLICs, HUD, and screen-size changes stay in the right place.

### Commit 6: `feat(video): add MSAA control`

Change:

- Add `MSAASamples = 0, 2, 4, 8` to `dethrace.ini`; default is 0.
- Set all SDL OpenGL attributes before the OpenGL window is made.
- Ask SDL for a multisample buffer and pass the real sample count as `BRT_MSAA_SAMPLES_I32`.
- If the request cannot be made, make one new context with MSAA off and give a clear log line.
- Report the requested and real value.

Check:

- 0 keeps the exact old result.
- 2, 4, and 8 smooth model edges on a fixed camera picture.
- An unsupported value falls back once and the game starts.
- Core OpenGL and OpenGL ES paths both get a check.

The SDL rules are in [SDL_GL_SetAttribute](https://wiki.libsdl.org/SDL2/SDL_GL_SetAttribute) and [SDL_GLattr](https://wiki.libsdl.org/SDL2/SDL_GLattr).

### Commit 7A in BRender: `feat(glrend): accept an anisotropy limit`

Change:

- Put one requested anisotropy limit in the present OpenGL device data.
- Clamp it to the GPU limit.
- Keep `1` as off and `0` as the current “GPU maximum” result.
- Keep the current material filter rules. Do not copy texture-state logic.

Check:

- Unit checks cover 0, 1, 2, 4, 8, 16, and a value above the GPU limit.
- Textures without mipmaps stay at 1.

### Commit 7B in Dethrace: `feat(video): add anisotropic filtering control`

Change:

- Add `Anisotropy = Max, 1, 2, 4, 8, 16` to `dethrace.ini`.
- Pass the value to BRender at OpenGL start.
- Update the BRender submodule pointer and nothing else in that commit.

Check:

- Road texture detail is compared at a low camera angle.
- The log gives the requested and clamped value.
- Software rendering has no change.

### Commit 8: `feat(video): add texture-filter modes`

This is a suggested change, not a need for anisotropy.

Change:

- Add `TextureFilter = Original, Nearest, Bilinear, Trilinear`.
- Map it to the present BRender filter and mip-filter state.
- Keep `Original` as the default.

Check:

- One near wall and one long road show the expected filter.
- Transparent texture edges do not get a new color line.

## Stage 3 - TrueType fonts

Gate before this stage:

- The user gives approval for a font set and its license.
- Make side-by-side pictures for every current game font. Do not select a font by name alone.

### Commit 9: `feat(fonts): make tDR_font atlases from TrueType`

Change:

- Put `stb_truetype.h` into the present `lib/stb` library. Do not add SDL_ttf or FreeType.
- At start, make the old `tDR_font` atlas and width table from an approved `.ttf` file.
- Keep `DRPixelmapText()` and `DRTextWidth()` as the one draw and measure path.
- Add `TrueTypeFonts = 0/1`; start with 0 as the safe default.
- Keep byte values 32 to 255. UTF-8 is a different possible change.

Check:

- Width, clipping, centering, and translated text have tests.
- Menu, race HUD, chat, result screen, and options screen have picture checks.
- No text goes out of its old box at 640x480.

### Commit 10: `feat(fonts): make br_font glyphs from TrueType`

Change:

- Make `BIGFONT`, `FONT7`, and `HEADUP` data in memory from the same approved font set.
- Keep `BrPixelmapText()` as the draw path.
- Do not remove the old `.FNT` reader.

Check:

- The old font-loading tests stay green.
- Start screens, debug text, and HUD text have picture checks.

### Commit 11: `feat(fonts): add high-resolution OpenGL text overlay`

This commit is optional and high danger.

Change:

- Draw only text that can be safely put after the old 2D overlay at drawable size.
- Keep old logical positions and scale them once.
- Do not move all old UI drawing into a new UI system.

Check:

- There is no double text.
- Screen capture, fade, map, menu, and FLIC paths still show text.
- 1080p and 4K text are sharper than the 640x480 atlas.

Stop rule:

- If safe coverage needs many call-site rules, stop at Commit 10. A full UI renderer is outside this QoL work.

## Stage 4 - PS One model conversion

This stage is on a separate branch. It starts only after an owned PS One image or unpacked data is given.

### Commit 12: `tools(psx): add an asset report tool`

Change:

- Read the disc tree and make a report of file names, sizes, hashes, and known chunk marks.
- Make no file guess from an extension alone.
- Write the found format facts into a short local format note.
- Do not put any game bytes in Git.

Check:

- The tool has fixed output for the same input.
- Short or bad files give a clear error and no crash.

Stop rule:

- If no model boundary can be proved, stop here and give the report. Do not make a guessed reader.

### Commit 13: `tools(psx): read one model into a neutral mesh`

Change:

- Read one proved model chunk into a small mesh form: positions, faces, UVs, material numbers, and actor parts.
- Check every offset and count at the file boundary.
- Use made-up test bytes, not game data, in unit tests.

Check:

- Vertex and face counts match an independent view in a PS One tool or emulator.
- Axis direction, scale, face order, and UV direction are written down.

### Commit 14: `tools(psx): write BRender DAT and ACT files`

Change:

- Use BRender's present model and actor save code.
- Convert the neutral mesh to `.DAT` and `.ACT`.
- Keep source names in a manifest and fail on name collisions.

Check:

- The result opens in a BRender model view.
- Dethrace loads it through the old loader with no new run-time model path.
- Bounds, normals, face order, and scale have tests.

### Commit 15: `tools(psx): convert model materials and textures`

Change:

- Convert proved palette and texture data to `.MAT` and `.PIX`.
- Keep color-key and transparent-pixel rules in one conversion function.

Check:

- A fixed model has the right UVs, palette, and transparent parts in Dethrace.
- No texture or material name silently replaces another one.

### Commit 16: `tools(psx): add manifest batch conversion`

Change:

- Convert only models named in a user-made manifest.
- Put output in a new directory. Never write into the owned source copy.
- Add a short user-owned-data note.

Check:

- A second run gives the same output hashes.
- A dry run gives the full write list.
- One bad model stops before any target file is replaced.

Why there is no direct PS One loader:

- An offline tool keeps PS One format work out of the game loop.
- The engine keeps one model path.
- Bad data cannot become a new run-time crash path.

## Stage 5 - other useful changes, each alone

Do these only after Stages 1 and 2 are stable.

### Commit 17: `feat(video): add VSync control`

- Values: off, on, adaptive when SDL gives support.
- Keep on as the current default.
- Check frame pace with the present FPS limit on and off.

### Commit 18: `feat(video): add aspect-correct widescreen`

- This is separate from high output size.
- Change the camera aspect and put old HUD art in a 4:3 safe area.
- Do not stretch the old HUD.
- Check 16:9, 16:10, 21:9, rear view, map, menus, mouse, and cut scenes.
- The current upstream request is [issue #349](https://github.com/dethrace-labs/dethrace/issues/349).

### Commit 19: `feat(content): add a read-only asset override directory`

- Search one user-set directory before original game data.
- Keep the original data directory as the only fallback.
- Give a clear list of used override files in the log.
- This can hold converted PS One files, font files, or texture work without changing the owned game copy.

### Conditional commit: `fix(video): improve OpenGL depth precision`

- Make this commit only if the longer view-distance test proves depth fighting.
- Ask for a 24-bit depth buffer and report the real value.
- Do not change the software depth path.

## Check matrix for every picture change

| Area | Required check |
|---|---|
| Build | Windows, Linux, macOS; SDL2; SDL3 when the changed path is shared |
| Tests | No-data tests, then full data tests with `DETHRACE_ROOT_DIR` |
| Modes | Software old result; OpenGL old default; OpenGL new value |
| Sizes | 640x480, 1280x960, 1920x1080, one high-DPI case |
| Game views | Front, rear, replay, map, cockpit, low-memory screen |
| Game data | Carmageddon, Splat Pack, one demo when possible |
| Picture | Fixed camera pictures with the same save and random seed |
| Speed | CPU frame time, GPU frame time, visible actor count, memory use |
| Save data | New run, old `OPTIONS.TXT`, old `dethrace.ini`, bad input value |

## Commit work rule

For every commit:

1. Show the one root cause in one sentence.
2. Name the files before making a change.
3. Run the old check.
4. Make the smallest change.
5. Run the new check and the full useful test set.
6. Read the diff and remove work not tied to that commit.
7. Commit only that change.
8. Give the commit hash, tests, picture result, speed result, and known limits.

If a BRender change is needed, make one BRender commit first. The Dethrace commit then changes only the submodule pointer and the Dethrace side of that same feature. Do not put two picture features in one submodule update.

## Current worktree care

The current tree has user files in `.idea/`, `Carma/`, `SDL2-2.32.8/`, and `cmake/zig-toolchain.cmake`. They are outside this plan and must not be added, changed, or removed.

The local `main` and current upstream `main` were both at `77713d88dc8526d13698a1b8c7fe9df840d3b442` when this plan was made on 2026-08-23.

For SDL, requested OpenGL attributes have to be set before the OpenGL window is made, and the drawable pixel size may be different from the window size. See [SDL_GL_SetAttribute](https://wiki.libsdl.org/SDL2/SDL_GL_SetAttribute) and [SDL_GL_GetDrawableSize](https://wiki.libsdl.org/SDL2/SDL_GL_GetDrawableSize).
