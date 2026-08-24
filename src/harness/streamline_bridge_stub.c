#include "harness/streamline_bridge.h"

int DethraceStreamlinePrepare(void) {
    return 0;
}

void* DethraceStreamlineGetInstanceProcAddr(void) {
    return 0;
}

void* DethraceStreamlineGetDeviceProcAddr(void* device, const char* name) {
    (void)device;
    (void)name;
    return 0;
}

int DethraceStreamlineSetVulkanPhysicalDevice(void* physical_device) {
    (void)physical_device;
    return 0;
}

int DethraceStreamlineEvaluate(const dethrace_streamline_frame* frame) {
    (void)frame;
    return 0;
}

void DethraceStreamlineSetMarker(int marker) {
    (void)marker;
}

void DethraceStreamlineSetFrameGenerationActive(int active, uint32_t render_width,
    uint32_t render_height, uint32_t display_width, uint32_t display_height) {
    (void)active;
    (void)render_width;
    (void)render_height;
    (void)display_width;
    (void)display_height;
}

void DethraceStreamlineShutdown(void) {
}
