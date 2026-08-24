#ifndef DETHRACE_STREAMLINE_BRIDGE_H
#define DETHRACE_STREAMLINE_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dethrace_streamline_image {
    void* image;
    void* memory;
    void* view;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t layout;
    uint32_t usage;
} dethrace_streamline_image;

typedef struct dethrace_streamline_frame {
    void* command_buffer;
    dethrace_streamline_image hudless_color;
    dethrace_streamline_image scaling_input_color;
    dethrace_streamline_image scaling_output_color;
    dethrace_streamline_image depth;
    dethrace_streamline_image motion;
    dethrace_streamline_image ui_color;
    dethrace_streamline_image backbuffer;
    uint32_t render_width;
    uint32_t render_height;
    uint32_t display_width;
    uint32_t display_height;
    uint64_t frame_index;
    const float* camera_view_to_clip;
    const float* clip_to_camera_view;
    const float* clip_to_prev_clip;
    const float* prev_clip_to_clip;
    float jitter_x;
    float jitter_y;
    float motion_scale_x;
    float motion_scale_y;
    int reset;
} dethrace_streamline_frame;

enum {
    DETHRACE_STREAMLINE_SR = 1,
    DETHRACE_STREAMLINE_FG = 2
};

int DethraceStreamlinePrepare(void);
void* DethraceStreamlineGetInstanceProcAddr(void);
void* DethraceStreamlineGetDeviceProcAddr(void* device, const char* name);
void DethraceStreamlineSetVulkanPhysicalDevice(void* physical_device);
void DethraceStreamlineSetVulkanInfo(void* instance, void* physical_device, void* device,
    uint32_t queue_family);
int DethraceStreamlineEvaluate(const dethrace_streamline_frame* frame);
void DethraceStreamlineShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
