#include "harness/streamline_bridge.h"

#if defined(_WIN32)

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <sl.h>
#include <sl_helpers_vk.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>

#include <windows.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {

using SlInit = PFun_slInit*;
using SlShutdown = PFun_slShutdown*;
using SlSetTagForFrame = PFun_slSetTagForFrame*;
using SlSetConstants = PFun_slSetConstants*;
using SlEvaluateFeature = PFun_slEvaluateFeature*;
using SlGetNewFrameToken = PFun_slGetNewFrameToken*;
using SlGetFeatureFunction = PFun_slGetFeatureFunction*;
using SlIsFeatureSupported = PFun_slIsFeatureSupported*;
using SlIsFeatureLoaded = PFun_slIsFeatureLoaded*;
using SlGetFeatureRequirements = PFun_slGetFeatureRequirements*;
using SlSetVulkanInfo = PFun_slSetVulkanInfo*;
using SlGetDeviceProcAddr = PFN_vkGetDeviceProcAddr;
using SlDLSSSetOptions = PFun_slDLSSSetOptions*;
using SlDLSSGGetState = PFun_slDLSSGGetState*;
using SlDLSSGSetOptions = PFun_slDLSSGSetOptions*;

struct State {
    HMODULE module = nullptr;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
    SlGetDeviceProcAddr get_device_proc_addr = nullptr;
    SlInit init = nullptr;
    SlShutdown shutdown = nullptr;
    SlSetTagForFrame set_tag_for_frame = nullptr;
    SlSetConstants set_constants = nullptr;
    SlEvaluateFeature evaluate_feature = nullptr;
    SlGetNewFrameToken get_new_frame_token = nullptr;
    SlGetFeatureFunction get_feature_function = nullptr;
    SlIsFeatureSupported is_feature_supported = nullptr;
    SlIsFeatureLoaded is_feature_loaded = nullptr;
    SlGetFeatureRequirements get_feature_requirements = nullptr;
    SlSetVulkanInfo set_vulkan_info = nullptr;
    SlDLSSSetOptions dlss_set_options = nullptr;
    SlDLSSGGetState dlssg_get_state = nullptr;
    SlDLSSGSetOptions dlssg_set_options = nullptr;
    sl::FrameToken* frame_token = nullptr;
    sl::ViewportHandle viewport{0};
    bool prepared = false;
    bool initialized = false;
    bool sr_enabled = false;
    bool fg_enabled = false;
    bool sr_options_set = false;
    bool fg_options_set = false;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    uint32_t render_width = 0;
    uint32_t render_height = 0;
    int mode = 2;
};

State g_state;

template <typename T>
T load_symbol(const char* name) {
    return g_state.module != nullptr
        ? reinterpret_cast<T>(GetProcAddress(g_state.module, name))
        : nullptr;
}

void log_result(const char* operation, sl::Result result) {
    if (result != sl::Result::eOk)
        std::fprintf(stderr, "STREAMLINE: %s failed (%u)\n", operation,
            static_cast<unsigned>(result));
}

bool env_enabled(const char* name, bool default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return default_value;
    return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
}

void choose_dlss_mode() {
    const char* value = std::getenv("DETHRACE_DLSS_MODE");
    if (value == nullptr)
        return;
    if (_stricmp(value, "quality") == 0)
        g_state.mode = 3;
    else if (_stricmp(value, "performance") == 0)
        g_state.mode = 1;
    else if (_stricmp(value, "ultra-performance") == 0)
        g_state.mode = 4;
    else if (_stricmp(value, "dlaa") == 0)
        g_state.mode = 6;
    else
        g_state.mode = 2;
}

void load_feature_functions() {
    if (g_state.get_feature_function == nullptr)
        return;

    void* function = nullptr;
    if (g_state.sr_enabled) {
        const sl::Result result = g_state.get_feature_function(sl::kFeatureDLSS,
            "slDLSSSetOptions", function);
        if (result == sl::Result::eOk)
            g_state.dlss_set_options = reinterpret_cast<SlDLSSSetOptions>(function);
        else {
            log_result("slDLSSSetOptions capability", result);
        }
    }

    function = nullptr;
    if (g_state.fg_enabled
        && g_state.get_feature_function(sl::kFeatureDLSS_G, "slDLSSGGetState", function) == sl::Result::eOk)
        g_state.dlssg_get_state = reinterpret_cast<SlDLSSGGetState>(function);

    function = nullptr;
    function = nullptr;
    if (g_state.fg_enabled) {
        const sl::Result result = g_state.get_feature_function(sl::kFeatureDLSS_G,
            "slDLSSGSetOptions", function);
        if (result == sl::Result::eOk)
            g_state.dlssg_set_options = reinterpret_cast<SlDLSSGSetOptions>(function);
        else {
            log_result("slDLSSGSetOptions capability", result);
            g_state.fg_enabled = false;
        }
    }
}

sl::Resource make_resource(const dethrace_streamline_image& source) {
    sl::Resource resource(sl::ResourceType::eTex2d, source.image, source.memory,
        source.view, source.layout);
    resource.width = source.width;
    resource.height = source.height;
    resource.nativeFormat = source.format;
    resource.mipLevels = 1;
    resource.arrayLayers = 1;
    resource.flags = 0;
    resource.usage = source.usage;
    return resource;
}

void copy_matrix(sl::float4x4& destination, const float* source) {
    if (source != nullptr)
        std::memcpy(destination.row, source, sizeof(destination.row));
}

void set_constants(const dethrace_streamline_frame& frame) {
    if (g_state.set_constants == nullptr || g_state.frame_token == nullptr)
        return;

    sl::Constants constants{};
    copy_matrix(constants.cameraViewToClip, frame.camera_view_to_clip);
    copy_matrix(constants.clipToCameraView, frame.clip_to_camera_view);
    copy_matrix(constants.clipToPrevClip, frame.clip_to_prev_clip);
    copy_matrix(constants.prevClipToClip, frame.prev_clip_to_clip);
    constants.jitterOffset = sl::float2(frame.jitter_x, frame.jitter_y);
    constants.mvecScale = sl::float2(frame.motion_scale_x, frame.motion_scale_y);
    constants.cameraNear = 0.1f;
    constants.cameraFar = 10000.0f;
    constants.cameraAspectRatio = frame.display_height != 0
        ? static_cast<float>(frame.display_width) / static_cast<float>(frame.display_height) : 1.0f;
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    constants.reset = frame.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    log_result("slSetConstants", g_state.set_constants(constants, *g_state.frame_token, g_state.viewport));
}

void configure_options(const dethrace_streamline_frame& frame) {
    if (g_state.sr_enabled && g_state.dlss_set_options != nullptr
        && (!g_state.sr_options_set || g_state.output_width != frame.display_width
            || g_state.output_height != frame.display_height)) {
        sl::DLSSOptions options{};
        options.mode = static_cast<sl::DLSSMode>(g_state.mode);
        options.outputWidth = frame.display_width;
        options.outputHeight = frame.display_height;
        options.colorBuffersHDR = sl::Boolean::eFalse;
        options.useAutoExposure = sl::Boolean::eTrue;
        log_result("slDLSSSetOptions", g_state.dlss_set_options(g_state.viewport, options));
        g_state.output_width = frame.display_width;
        g_state.output_height = frame.display_height;
        g_state.sr_options_set = true;
    }

    if (g_state.fg_enabled && g_state.dlssg_set_options != nullptr
        && (!g_state.fg_options_set || g_state.render_width != frame.render_width
            || g_state.render_height != frame.render_height
            || g_state.output_width != frame.display_width
            || g_state.output_height != frame.display_height)) {
        sl::DLSSGOptions options{};
        options.mode = sl::DLSSGMode::eOn;
        options.numFramesToGenerate = 1;
        options.numBackBuffers = 2;
        options.mvecDepthWidth = frame.render_width;
        options.mvecDepthHeight = frame.render_height;
        options.colorWidth = frame.display_width;
        options.colorHeight = frame.display_height;
        options.enableUserInterfaceRecomposition = sl::Boolean::eTrue;
        log_result("slDLSSGSetOptions", g_state.dlssg_set_options(g_state.viewport, options));
        g_state.render_width = frame.render_width;
        g_state.render_height = frame.render_height;
        g_state.fg_options_set = true;
    }
}

} // namespace

extern "C" int DethraceStreamlinePrepare(void) {
    if (g_state.prepared)
        return g_state.initialized ? 1 : 0;
    g_state.prepared = true;

    if (!env_enabled("DETHRACE_STREAMLINE", true))
        return 0;

    const char* path = std::getenv("DETHRACE_STREAMLINE_PATH");
    g_state.module = LoadLibraryA(path != nullptr && path[0] != '\0' ? path : "sl.interposer.dll");
    if (g_state.module == nullptr) {
        std::fprintf(stderr, "STREAMLINE: sl.interposer.dll not found; using Vulkan fallback\n");
        return 0;
    }

    g_state.get_instance_proc_addr = load_symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    g_state.get_device_proc_addr = load_symbol<SlGetDeviceProcAddr>("vkGetDeviceProcAddr");
    g_state.init = load_symbol<SlInit>("slInit");
    g_state.shutdown = load_symbol<SlShutdown>("slShutdown");
    g_state.set_tag_for_frame = load_symbol<SlSetTagForFrame>("slSetTagForFrame");
    g_state.set_constants = load_symbol<SlSetConstants>("slSetConstants");
    g_state.evaluate_feature = load_symbol<SlEvaluateFeature>("slEvaluateFeature");
    g_state.get_new_frame_token = load_symbol<SlGetNewFrameToken>("slGetNewFrameToken");
    g_state.get_feature_function = load_symbol<SlGetFeatureFunction>("slGetFeatureFunction");
    g_state.is_feature_supported = load_symbol<SlIsFeatureSupported>("slIsFeatureSupported");
    g_state.is_feature_loaded = load_symbol<SlIsFeatureLoaded>("slIsFeatureLoaded");
    g_state.get_feature_requirements = load_symbol<SlGetFeatureRequirements>("slGetFeatureRequirements");
    g_state.set_vulkan_info = load_symbol<SlSetVulkanInfo>("slSetVulkanInfo");
    if (g_state.get_instance_proc_addr == nullptr || g_state.get_device_proc_addr == nullptr
        || g_state.init == nullptr
        || g_state.shutdown == nullptr || g_state.set_tag_for_frame == nullptr
        || g_state.set_constants == nullptr || g_state.evaluate_feature == nullptr
        || g_state.get_new_frame_token == nullptr || g_state.get_feature_function == nullptr) {
        std::fprintf(stderr, "STREAMLINE: incomplete interposer API; using Vulkan fallback\n");
        FreeLibrary(g_state.module);
        g_state.module = nullptr;
        return 0;
    }

    choose_dlss_mode();
    const bool want_sr = env_enabled("DETHRACE_DLSS", true);
    const bool want_fg = env_enabled("DETHRACE_DLSSG", true);
    sl::Feature features[2]{};
    uint32_t feature_count = 0;
    if (want_sr) features[feature_count++] = sl::kFeatureDLSS;
    if (want_fg) features[feature_count++] = sl::kFeatureDLSS_G;

    sl::Preferences preferences{};
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "dethrace-vulkan";
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = feature_count;
    preferences.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    if (env_enabled("DETHRACE_STREAMLINE_CONSOLE", false))
        preferences.showConsole = true;

    const sl::Result result = g_state.init(preferences, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        log_result("slInit", result);
        FreeLibrary(g_state.module);
        g_state.module = nullptr;
        return 0;
    }

    if (g_state.is_feature_loaded != nullptr) {
        bool loaded = false;
        if (want_sr) {
            const sl::Result feature_result = g_state.is_feature_loaded(sl::kFeatureDLSS, loaded);
            log_result("slIsFeatureLoaded(DLSS)", feature_result);
            std::fprintf(stderr, "STREAMLINE: DLSS feature loaded=%s\n", loaded ? "yes" : "no");
        }
        loaded = false;
        if (want_fg) {
            const sl::Result feature_result = g_state.is_feature_loaded(sl::kFeatureDLSS_G, loaded);
            log_result("slIsFeatureLoaded(DLSS-G)", feature_result);
            std::fprintf(stderr, "STREAMLINE: DLSS-G feature loaded=%s\n", loaded ? "yes" : "no");
        }
    }
    if (g_state.get_feature_requirements != nullptr && want_sr) {
        sl::FeatureRequirements requirements{};
        log_result("slGetFeatureRequirements(DLSS)",
            g_state.get_feature_requirements(sl::kFeatureDLSS, requirements));
    }

    g_state.sr_enabled = want_sr;
    g_state.fg_enabled = want_fg;
    g_state.initialized = true;
    std::fprintf(stderr, "STREAMLINE: initialized (DLSS=%s, DLSS-G=%s)\n",
        want_sr ? "requested" : "off", want_fg ? "requested" : "off");
    return 1;
}

extern "C" void* DethraceStreamlineGetInstanceProcAddr(void) {
    return g_state.initialized && g_state.get_instance_proc_addr != nullptr
        ? reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(g_state.get_instance_proc_addr)) : nullptr;
}

extern "C" void* DethraceStreamlineGetDeviceProcAddr(void* device, const char* name) {
    if (!g_state.initialized || g_state.get_device_proc_addr == nullptr)
        return nullptr;
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(g_state.get_device_proc_addr(
        reinterpret_cast<VkDevice>(device), name)));
}

extern "C" void DethraceStreamlineSetVulkanPhysicalDevice(void* physical_device) {
    if (!g_state.initialized || g_state.is_feature_supported == nullptr)
        return;
    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = physical_device;
    if (g_state.sr_enabled)
        log_result("slIsFeatureSupported(DLSS)",
            g_state.is_feature_supported(sl::kFeatureDLSS, adapter));
    if (g_state.fg_enabled)
        log_result("slIsFeatureSupported(DLSS-G)",
            g_state.is_feature_supported(sl::kFeatureDLSS_G, adapter));
}

extern "C" void DethraceStreamlineSetVulkanInfo(void* instance, void* physical_device,
    void* device, uint32_t queue_family) {
    if (!g_state.initialized || g_state.set_vulkan_info == nullptr)
        return;
    sl::VulkanInfo info{};
    info.instance = reinterpret_cast<VkInstance>(instance);
    info.physicalDevice = reinterpret_cast<VkPhysicalDevice>(physical_device);
    info.device = reinterpret_cast<VkDevice>(device);
    info.graphicsQueueFamily = queue_family;
    info.graphicsQueueIndex = 0;
    info.computeQueueFamily = queue_family;
    info.computeQueueIndex = 0;
    log_result("slSetVulkanInfo", g_state.set_vulkan_info(info));
}

extern "C" int DethraceStreamlineEvaluate(const dethrace_streamline_frame* frame) {
    if (!g_state.initialized || frame == nullptr || frame->command_buffer == nullptr)
        return 0;

    const uint32_t frame_index = static_cast<uint32_t>(frame->frame_index);
    if (g_state.get_new_frame_token(g_state.frame_token, &frame_index) != sl::Result::eOk
        || g_state.frame_token == nullptr)
        return 0;

    load_feature_functions();
    configure_options(*frame);
    set_constants(*frame);

    sl::Resource hudless = make_resource(frame->hudless_color);
    sl::Resource input = make_resource(frame->scaling_input_color);
    sl::Resource output = make_resource(frame->scaling_output_color);
    sl::Resource depth = make_resource(frame->depth);
    sl::Resource motion = make_resource(frame->motion);
    sl::Resource ui = make_resource(frame->ui_color);
    sl::Resource backbuffer = make_resource(frame->backbuffer);
    sl::Extent render_extent{0, 0, frame->render_width, frame->render_height};
    sl::Extent display_extent{0, 0, frame->display_width, frame->display_height};
    sl::ResourceTag tags[7]{};
    uint32_t tag_count = 0;
    if (g_state.sr_enabled)
        tags[tag_count++] = sl::ResourceTag(&input, sl::kBufferTypeScalingInputColor,
            sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent);
    if (g_state.sr_enabled)
        tags[tag_count++] = sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor,
            sl::ResourceLifecycle::eValidUntilEvaluate, &display_extent);
    tags[tag_count++] = sl::ResourceTag(&depth, sl::kBufferTypeDepth,
        sl::ResourceLifecycle::eValidUntilPresent, &render_extent);
    tags[tag_count++] = sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors,
        sl::ResourceLifecycle::eValidUntilPresent, &render_extent);
    if (g_state.fg_enabled) {
        tags[tag_count++] = sl::ResourceTag(&hudless, sl::kBufferTypeHUDLessColor,
            sl::ResourceLifecycle::eValidUntilPresent, &display_extent);
        tags[tag_count++] = sl::ResourceTag(&ui, sl::kBufferTypeUIColorAndAlpha,
            sl::ResourceLifecycle::eValidUntilPresent, &display_extent);
        tags[tag_count++] = sl::ResourceTag(&backbuffer, sl::kBufferTypeBackbuffer,
            sl::ResourceLifecycle::eValidUntilPresent, &display_extent);
    }

    sl::CommandBuffer* command_buffer = reinterpret_cast<sl::CommandBuffer*>(frame->command_buffer);
    const sl::Result tag_result = g_state.set_tag_for_frame(*g_state.frame_token,
        g_state.viewport, tags, tag_count, command_buffer);
    if (tag_result != sl::Result::eOk) {
        log_result("slSetTagForFrame", tag_result);
        return 0;
    }

    const sl::BaseStructure* inputs_for_feature[] = {&g_state.viewport};
    int result_mask = 0;
    if (g_state.sr_enabled) {
        const sl::Result result = g_state.evaluate_feature(sl::kFeatureDLSS,
            *g_state.frame_token, inputs_for_feature, 1, command_buffer);
        if (result == sl::Result::eOk)
            result_mask |= DETHRACE_STREAMLINE_SR;
        else
            log_result("slEvaluateFeature(DLSS)", result);
    }
    if (g_state.fg_enabled && (!g_state.sr_enabled || (result_mask & DETHRACE_STREAMLINE_SR) != 0))
        result_mask |= DETHRACE_STREAMLINE_FG;
    return result_mask;
}

extern "C" void DethraceStreamlineShutdown(void) {
    if (g_state.initialized && g_state.shutdown != nullptr)
        log_result("slShutdown", g_state.shutdown());
    g_state.initialized = false;
    g_state.prepared = false;
    g_state.frame_token = nullptr;
    if (g_state.module != nullptr)
        FreeLibrary(g_state.module);
    g_state = State{};
}

#else

extern "C" int DethraceStreamlinePrepare(void) { return 0; }
extern "C" void* DethraceStreamlineGetInstanceProcAddr(void) { return nullptr; }
extern "C" void* DethraceStreamlineGetDeviceProcAddr(void*, const char*) { return nullptr; }
extern "C" void DethraceStreamlineSetVulkanPhysicalDevice(void*) {}
extern "C" void DethraceStreamlineSetVulkanInfo(void*, void*, void*, uint32_t) {}
extern "C" int DethraceStreamlineEvaluate(const dethrace_streamline_frame*) { return 0; }
extern "C" void DethraceStreamlineShutdown(void) {}

#endif
