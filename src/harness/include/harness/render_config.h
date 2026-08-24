#ifndef HARNESS_RENDER_CONFIG_H
#define HARNESS_RENDER_CONFIG_H

/*
 * Optional 3D scene render size, separate from the game's own 640x480 screen.
 *
 * Set DETHRACE_VULKAN_SCENE_SIZE=<width>x<height> to render the 3D scene at
 * that size instead of the game's screen size. A 16:9 value such as 960x540
 * gives a widescreen scene that DLSS can then upscale by a whole factor
 * (960x540 -> 1920x1080 is exactly 2x, which is DLSS performance mode).
 *
 * The 2D layer stays at the game's own size and is composited without being
 * stretched, so the HUD and menus keep their 4:3 shape.
 *
 * With the variable unset, everything behaves exactly as before.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 and fills width/height when an override is set, 0 otherwise. */
int HarnessRenderSceneSize(int* width, int* height);

/* Scene aspect ratio to give the game's forward cameras, or 0 when unset. */
float HarnessRenderSceneAspect(void);

/*
 * Re-applies the scene aspect to the game's forward cameras. The game resets
 * that field whenever the view changes, so this runs once a frame rather than
 * at camera creation. Does nothing when no override is set.
 */
void HarnessRenderApplyCameraAspect(void);

#ifdef __cplusplus
}
#endif

#endif
