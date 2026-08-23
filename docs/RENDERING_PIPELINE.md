# Rendering pipeline

The original game renders both 2d and 3d elements to the same memory buffer, called `gBack_screen`. 

Another variable, `gRender_screen`, points into that memory buffer and is where the 3d scene is drawn on top of any existing 2d pixels.

Rendering is done in a (standard for the time) 8 bit paletted mode.

```
+-----------------------------------------+
|gBack_screen                             |
|      +------------------------+         |
|      | gRender_screen         |         |
|      |                        |         | 
|      |                        |         |
|      |                        |         |
|      |                        |         |
|      +------------------------+         |
|                                         |
+-----------------------------------------+
```

The `RenderAFrame` function does the following:

1. Render 2d background content (horizon, map, etc) to `gBack_screen`
2. Start 3d scene rendering
3. Render 3d environment to `gRender_screen`
4. End 3d scene rendering
5. Render 2d foreground content into `gBack_screen` (HUD, messages, etc)
6. Swap buffers

If the rearview mirror is rendered, steps 3-4 are repeated, this time rendering into `gRearview_screen`

## Palette manipulation

The game palette is updated frequently:
1. Fade screen to black
2. Fade screen back up to normal brightness
3. Different palettes for the menu interface and for game play
4. The "On drugs" powerup

Palette animations run in a tight loop, assuming they are writing to the system color palette, so do not re-render the 3d scene etc. We handle this by hooking the palette functions and reusing the last-rendered scene from our framebuffer.

## OpenGL implementation

### Start 3d rendering hook
- Capture the current `gBack_screen` and convert it to a 32 bit OpenGL texture, and render it as a full-screen quad.
- Configure OpenGL framebuffer to do render-to-texture 
- Clear `gBack_screen`

### Render model hook
- Render the model as an OpenGL VBO, convert referenced materials to OpenGL textures.

### End 3d rendering hook
Render the framebuffer from above as a full-screen quad.

### Swap buffers hook
- Again capture `gBack_screen` to pick up HUD elements rendered after the 3d scene, convert it to 32 bit, and render it as a full-screen quad.
- Generate a palette-manipulation image which is blended over the top of everything as a full-screen quad to handle palette animations.

Transparent sprite maps keep the base texture level in the OpenGL path. This keeps pedestrians and effects at their highest available detail at any distance, while opaque world textures still use mipmaps and anisotropic filtering.

## Vulkan implementation

`--vulkan` keeps the original ordering but separates the GPU targets that the old paletted
buffer combined:

1. CPU 2D work is staged into an RGB565 upload image.
2. 3D stored models render into a HUD-less colour image with a matching depth image.
3. The final HUD and other 2D work are composited over that scene before presentation.
4. The rear-view mirror uses the same scene target with an active sub-pixelmap rectangle;
   map mode remains a normal 2D-over-3D composition.

Stage 4 groundwork is live behind `DETHRACE_VULKAN_RENDER_SCALE` (default `1.0`, accepted
range `0.5` to `1.0`). At a lower value the Vulkan scene target is smaller, the scene is
upscaled into the swapchain, and the RGB565 UI upload is sampled at display resolution after
that upscale. The depth attachment includes `VK_IMAGE_USAGE_SAMPLED_BIT`. The driver exposes
HUD-less colour, depth, motion, and UI image views through an internal `vk_dlss_resources`
contract; this keeps future Streamline integration separate from BRender and from the Vulkan
loader seam. Stored-model motion is written to `R16G16_SFLOAT` from actor/geometry/group
history; first-use and 2D paths write zero. An 8-sample Halton sequence offsets only the render
projection, while unjittered matrices feed motion. Streamline evaluation is not claimed yet.

The scene colour, depth, and motion-vector targets are deliberately separate. This is
the resource boundary needed for NVIDIA Streamline DLSS Super Resolution and Vulkan 2x Frame
Generation in the later stages. The current Vulkan path has no Streamline binaries or DLSS
evaluation call yet; unsupported hardware must continue to use the plain Vulkan fallback.

The repeatable Race 0 benchmark is `tools/benchmark_renderers.ps1`. Set
`DETHRACE_FRAME_STATS` to collect presentation intervals from either renderer; Vulkan records
at its direct `vkQueuePresentKHR` boundary, while OpenGL records at the SDL swap boundary.
