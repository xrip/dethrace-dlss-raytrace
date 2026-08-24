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

void DethraceStreamlineSetVulkanPhysicalDevice(void* physical_device) {
    (void)physical_device;
}

void DethraceStreamlineSetVulkanInfo(void* instance, void* physical_device, void* device,
    uint32_t queue_family) {
    (void)instance;
    (void)physical_device;
    (void)device;
    (void)queue_family;
}

int DethraceStreamlineEvaluate(const dethrace_streamline_frame* frame) {
    (void)frame;
    return 0;
}

void DethraceStreamlineShutdown(void) {
}
