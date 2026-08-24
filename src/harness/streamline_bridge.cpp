#include "harness/streamline_bridge.h"

#if defined(_WIN32)

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <sl.h>
#include <sl_helpers_vk.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

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
using SlGetDeviceProcAddr = PFN_vkGetDeviceProcAddr;
using SlDLSSSetOptions = PFun_slDLSSSetOptions*;
using SlDLSSGGetState = PFun_slDLSSGGetState*;
using SlDLSSGSetOptions = PFun_slDLSSGSetOptions*;
using SlPCLSetMarker = PFun_slPCLSetMarker*;
using SlReflexSetOptions = PFun_slReflexSetOptions*;
using SlReflexSleep = PFun_slReflexSleep*;

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
    SlDLSSSetOptions dlss_set_options = nullptr;
    SlDLSSGGetState dlssg_get_state = nullptr;
    SlDLSSGSetOptions dlssg_set_options = nullptr;
    SlPCLSetMarker pcl_set_marker = nullptr;
    SlReflexSetOptions reflex_set_options = nullptr;
    SlReflexSleep reflex_sleep = nullptr;
    sl::FrameToken* frame_token = nullptr;
    sl::ViewportHandle viewport{0};
    bool prepared = false;
    bool initialized = false;
    bool sr_enabled = false;
    bool fg_enabled = false;
    bool sr_options_set = false;
    bool fg_options_set = false;
    bool fg_runtime_on = false;
    bool pcl_enabled = false;
    bool reflex_enabled = false;
    bool reflex_options_set = false;
    bool frame_active = false;
    bool simulation_active = false;
    bool first_frame_logged = false;
    bool feature_functions_checked = false;
    uint32_t fg_present_calls = 0;
    uint64_t fg_presented_frames = 0;
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

void streamline_log(sl::LogType type, const char* message) {
    if (type == sl::LogType::eInfo) {
        const char* debug = std::getenv("DETHRACE_STREAMLINE_DEBUG");
        if (debug == nullptr || (debug[0] != '1' && debug[0] != 'y' && debug[0] != 'Y'))
            return;
    }
    const char* level = type == sl::LogType::eError ? "error"
        : (type == sl::LogType::eWarn ? "warn" : "info");
    std::fprintf(stderr, "STREAMLINE: [sdk:%s] %s\n", level,
        message != nullptr ? message : "");
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
    if (g_state.get_feature_function == nullptr || g_state.feature_functions_checked)
        return;
    g_state.feature_functions_checked = true;

    void* function = nullptr;
    if (g_state.sr_enabled) {
        const sl::Result result = g_state.get_feature_function(sl::kFeatureDLSS,
            "slDLSSSetOptions", function);
        if (result == sl::Result::eOk)
            g_state.dlss_set_options = reinterpret_cast<SlDLSSSetOptions>(function);
        else {
            log_result("slDLSSSetOptions capability", result);
            g_state.sr_enabled = false;
        }
    }

    function = nullptr;
    if (g_state.fg_enabled
        && g_state.get_feature_function(sl::kFeatureDLSS_G, "slDLSSGGetState", function) == sl::Result::eOk)
        g_state.dlssg_get_state = reinterpret_cast<SlDLSSGGetState>(function);

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

    function = nullptr;
    if (g_state.fg_enabled) {
        const sl::Result result = g_state.get_feature_function(sl::kFeatureReflex,
            "slReflexSetOptions", function);
        if (result == sl::Result::eOk)
            g_state.reflex_set_options = reinterpret_cast<SlReflexSetOptions>(function);
        else {
            log_result("slReflexSetOptions capability", result);
            g_state.fg_enabled = false;
        }
    }

    function = nullptr;
    if (g_state.fg_enabled) {
        const sl::Result result = g_state.get_feature_function(sl::kFeatureReflex,
            "slReflexSleep", function);
        if (result == sl::Result::eOk) {
            g_state.reflex_sleep = reinterpret_cast<SlReflexSleep>(function);
            g_state.reflex_enabled = true;
        } else {
            log_result("slReflexSleep capability", result);
            g_state.fg_enabled = false;
        }
    }

    function = nullptr;
    if (g_state.fg_enabled) {
        const sl::Result result = g_state.get_feature_function(sl::kFeaturePCL,
            "slPCLSetMarker", function);
        if (result == sl::Result::eOk) {
            g_state.pcl_set_marker = reinterpret_cast<SlPCLSetMarker>(function);
            g_state.pcl_enabled = true;
        } else {
            log_result("slPCLSetMarker capability", result);
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
    constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
    if (frame.camera_pos != nullptr)
        constants.cameraPos = sl::float3(frame.camera_pos[0], frame.camera_pos[1], frame.camera_pos[2]);
    if (frame.camera_up != nullptr)
        constants.cameraUp = sl::float3(frame.camera_up[0], frame.camera_up[1], frame.camera_up[2]);
    if (frame.camera_right != nullptr)
        constants.cameraRight = sl::float3(frame.camera_right[0], frame.camera_right[1], frame.camera_right[2]);
    if (frame.camera_fwd != nullptr)
        constants.cameraFwd = sl::float3(frame.camera_fwd[0], frame.camera_fwd[1], frame.camera_fwd[2]);
    constants.cameraNear = 0.1f;
    constants.cameraFar = 10000.0f;
    constants.cameraFOV = frame.camera_fov > 0.0f ? frame.camera_fov : 1.0f;
    constants.cameraAspectRatio = frame.display_height != 0
        ? static_cast<float>(frame.display_width) / static_cast<float>(frame.display_height) : 1.0f;
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    constants.reset = frame.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    log_result("slSetConstants", g_state.set_constants(constants, *g_state.frame_token, g_state.viewport));
}

bool configure_reflex() {
    if (g_state.fg_enabled && g_state.reflex_set_options != nullptr
        && !g_state.reflex_options_set) {
        sl::ReflexOptions options{};
        options.mode = sl::ReflexMode::eLowLatency;
        const sl::Result result = g_state.reflex_set_options(options);
        log_result("slReflexSetOptions", result);
        g_state.reflex_options_set = result == sl::Result::eOk;
        if (!g_state.reflex_options_set)
            g_state.fg_enabled = false;
    }

    return g_state.fg_enabled && g_state.reflex_options_set;
}

void configure_options(const dethrace_streamline_frame& frame) {
    configure_reflex();

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
        && (!g_state.fg_options_set || !g_state.fg_runtime_on
            || g_state.render_width != frame.render_width
            || g_state.render_height != frame.render_height
            || g_state.output_width != frame.display_width
            || g_state.output_height != frame.display_height)) {
        sl::DLSSGOptions options{};
        options.mode = sl::DLSSGMode::eOn;
        options.numFramesToGenerate = 1;
        log_result("slDLSSGSetOptions", g_state.dlssg_set_options(g_state.viewport, options));
        g_state.render_width = frame.render_width;
        g_state.render_height = frame.render_height;
        g_state.fg_options_set = true;
        if (!g_state.fg_runtime_on) {
            g_state.fg_present_calls = 0;
            g_state.fg_presented_frames = 0;
        }
        g_state.fg_runtime_on = true;
    }
}

/* slIsFeatureLoaded and slGetFeatureRequirements both need the Vulkan device to
 * exist (sl_core_api.h:117). Asking during slInit made the plug-ins answer from
 * an uninitialized NGX, so this runs once the device has been created. */
void report_feature_state(sl::Feature feature, const char* name) {
    if (g_state.is_feature_loaded != nullptr) {
        bool loaded = false;
        log_result("slIsFeatureLoaded", g_state.is_feature_loaded(feature, loaded));
        std::fprintf(stderr, "STREAMLINE: %s feature loaded=%s\n", name, loaded ? "yes" : "no");
    }
    if (g_state.get_feature_requirements != nullptr) {
        sl::FeatureRequirements requirements{};
        const sl::Result result = g_state.get_feature_requirements(feature, requirements);
        log_result("slGetFeatureRequirements", result);
        std::fprintf(stderr, "STREAMLINE: %s requirements result=%u flags=%u\n", name,
            static_cast<unsigned>(result), static_cast<unsigned>(requirements.flags));
    }
}

void query_dlssg_state(bool count_present) {
    if (!g_state.fg_runtime_on || g_state.dlssg_get_state == nullptr)
        return;

    sl::DLSSGState state{};
    const sl::Result result = g_state.dlssg_get_state(g_state.viewport, state, nullptr);
    if (result != sl::Result::eOk) {
        log_result("slDLSSGGetState", result);
        return;
    }
    if (!count_present)
        return;

    g_state.fg_present_calls++;
    g_state.fg_presented_frames += state.numFramesActuallyPresented;
    if (g_state.fg_present_calls % 30 == 0) {
        std::fprintf(stderr,
            "STREAMLINE: DLSS-G state status=%u presented=%llu host-presents=%u max-generated=%u\n",
            static_cast<unsigned>(state.status),
            static_cast<unsigned long long>(g_state.fg_presented_frames),
            g_state.fg_present_calls, state.numFramesToGenerateMax);
    }
}

} // namespace

extern "C" int DethraceStreamlinePrepare(void) {
    if (g_state.prepared)
        return g_state.initialized ? 1 : 0;
    g_state.prepared = true;

    if (!env_enabled("DETHRACE_STREAMLINE", true))
        return 0;

    choose_dlss_mode();
    /* Do not install Streamline's Vulkan hooks unless a feature was requested. */
    const bool want_sr = env_enabled("DETHRACE_DLSS", false);
    const bool want_fg = env_enabled("DETHRACE_DLSSG", false);
    if (!want_sr && !want_fg)
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

    sl::Feature features[4]{};
    uint32_t feature_count = 0;
    if (want_sr) features[feature_count++] = sl::kFeatureDLSS;
    if (want_fg) {
        features[feature_count++] = sl::kFeatureReflex;
        features[feature_count++] = sl::kFeaturePCL;
        features[feature_count++] = sl::kFeatureDLSS_G;
    }

    sl::Preferences preferences{};
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "dethrace-vulkan";
    const char* application_id = std::getenv("DETHRACE_STREAMLINE_APP_ID");
    preferences.applicationId = application_id != nullptr && application_id[0] != '\0'
        ? static_cast<uint32_t>(std::strtoul(application_id, nullptr, 10)) : 231313132u;
    const char* project_id = std::getenv("DETHRACE_STREAMLINE_PROJECT_ID");
    if (project_id != nullptr && project_id[0] != '\0')
    preferences.projectId = project_id;
    preferences.logMessageCallback = streamline_log;
    preferences.logLevel = env_enabled("DETHRACE_STREAMLINE_DEBUG", false)
        ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = feature_count;
    /* Keep the bundled SDK and plug-ins as one versioned set. The SDK default
     * opts into OTA plug-ins, but a 2.11 cache beside the 2.12 package can
     * mix ABI versions and break NGX initialization.
     *
     * eDisableCLStateTracking is part of the SDK default and must be kept:
     * assigning only the tagging flag drops it, and the plug-ins then report
     * missing command-list state hooks on the Vulkan path. */
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking
        | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    if (env_enabled("DETHRACE_STREAMLINE_CONSOLE", false))
        preferences.showConsole = true;

    const sl::Result result = g_state.init(preferences, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        log_result("slInit", result);
        FreeLibrary(g_state.module);
        g_state.module = nullptr;
        return 0;
    }

    g_state.sr_enabled = want_sr;
    g_state.fg_enabled = want_fg;
    g_state.pcl_enabled = false;
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

extern "C" int DethraceStreamlineCreateWin32Surface(void* instance, void* window,
    void* window_instance, void** surface) {
    if (!g_state.initialized || instance == nullptr || window == nullptr
        || window_instance == nullptr || surface == nullptr)
        return 0;
    /* This entry point is intentionally not returned by Streamline's
     * vkGetInstanceProcAddr. NVIDIA's Vulkan sample calls the interposer export
     * directly so its surface-to-HWND hook can record the window. */
    const auto create_surface = load_symbol<PFN_vkCreateWin32SurfaceKHR>(
        "vkCreateWin32SurfaceKHR");
    if (create_surface == nullptr)
        return 0;

    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = reinterpret_cast<HINSTANCE>(window_instance);
    info.hwnd = reinterpret_cast<HWND>(window);
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    if (create_surface(reinterpret_cast<VkInstance>(instance), &info, nullptr, &vk_surface) != VK_SUCCESS)
        return 0;
    *surface = reinterpret_cast<void*>(vk_surface);
    return 1;
}

extern "C" int DethraceStreamlineSetVulkanPhysicalDevice(void* physical_device) {
    if (!g_state.initialized || g_state.is_feature_supported == nullptr)
        return 0;
    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = physical_device;
    if (g_state.sr_enabled)
        report_feature_state(sl::kFeatureDLSS, "DLSS");
    if (g_state.fg_enabled)
        report_feature_state(sl::kFeatureDLSS_G, "DLSS-G");
    if (g_state.sr_enabled) {
        const sl::Result result = g_state.is_feature_supported(sl::kFeatureDLSS, adapter);
        log_result("slIsFeatureSupported(DLSS)", result);
        if (result != sl::Result::eOk)
            g_state.sr_enabled = false;
    }
    if (g_state.fg_enabled) {
        const sl::Result reflex_result = g_state.is_feature_supported(sl::kFeatureReflex, adapter);
        log_result("slIsFeatureSupported(Reflex)", reflex_result);
        const sl::Result pcl_result = g_state.is_feature_supported(sl::kFeaturePCL, adapter);
        log_result("slIsFeatureSupported(PCL)", pcl_result);
        const sl::Result fg_result = g_state.is_feature_supported(sl::kFeatureDLSS_G, adapter);
        log_result("slIsFeatureSupported(DLSS-G)", fg_result);
        if (reflex_result != sl::Result::eOk || pcl_result != sl::Result::eOk
            || fg_result != sl::Result::eOk)
            g_state.fg_enabled = false;
    }
    load_feature_functions();

    /* Force sl.dlss_g to start up now, while the caller has not yet created the
     * swapchain. Plug-in startup is what registers slHookVkCreateSwapchainKHR,
     * and DLSS-G can only interpolate into a swapchain it created itself. */
    if (g_state.fg_enabled && g_state.dlssg_set_options != nullptr) {
        sl::DLSSGOptions options{};
        options.mode = sl::DLSSGMode::eOn;
        options.numFramesToGenerate = 1;
        log_result("slDLSSGSetOptions(startup)", g_state.dlssg_set_options(g_state.viewport, options));
        g_state.fg_runtime_on = true;
        g_state.fg_options_set = true;
    }
    return (g_state.sr_enabled || g_state.fg_enabled) ? 1 : 0;
}

extern "C" int DethraceStreamlineBeginFrame(void) {
    if (!g_state.initialized || !g_state.fg_enabled)
        return 0;

    load_feature_functions();
    query_dlssg_state(true);
    if (!configure_reflex()
        || g_state.get_new_frame_token(g_state.frame_token, nullptr) != sl::Result::eOk
        || g_state.frame_token == nullptr)
        return 0;

    g_state.frame_active = true;
    log_result("slReflexSleep", g_state.reflex_sleep(*g_state.frame_token));
    log_result("slPCLSetMarker(SimulationStart)",
        g_state.pcl_set_marker(sl::PCLMarker::eSimulationStart, *g_state.frame_token));
    g_state.simulation_active = true;
    return 1;
}

extern "C" void DethraceStreamlineEndSimulation(void) {
    if (!g_state.simulation_active || g_state.frame_token == nullptr)
        return;
    log_result("slPCLSetMarker(SimulationEnd)",
        g_state.pcl_set_marker(sl::PCLMarker::eSimulationEnd, *g_state.frame_token));
    g_state.simulation_active = false;
}

extern "C" int DethraceStreamlineEvaluate(const dethrace_streamline_frame* frame) {
    if (!g_state.initialized || frame == nullptr || frame->command_buffer == nullptr)
        return 0;

    load_feature_functions();
    if (!g_state.frame_active) {
        const uint32_t frame_index = static_cast<uint32_t>(frame->frame_index);
        if (g_state.get_new_frame_token(g_state.frame_token, &frame_index) != sl::Result::eOk
            || g_state.frame_token == nullptr)
            return 0;
        g_state.frame_active = true;
        if (configure_reflex()) {
            log_result("slReflexSleep", g_state.reflex_sleep(*g_state.frame_token));
            log_result("slPCLSetMarker(SimulationStart fallback)",
                g_state.pcl_set_marker(sl::PCLMarker::eSimulationStart, *g_state.frame_token));
            log_result("slPCLSetMarker(SimulationEnd fallback)",
                g_state.pcl_set_marker(sl::PCLMarker::eSimulationEnd, *g_state.frame_token));
        }
    }
    configure_options(*frame);
    set_constants(*frame);

    sl::Resource input = make_resource(frame->scaling_input_color);
    sl::Resource output = make_resource(frame->scaling_output_color);
    sl::Resource hudless = g_state.sr_enabled ? output : make_resource(frame->hudless_color);
    sl::Resource depth = make_resource(frame->depth);
    sl::Resource motion = make_resource(frame->motion);
    sl::Resource ui = make_resource(frame->ui_color);
    sl::Extent render_extent{0, 0, frame->render_width, frame->render_height};
    sl::Extent display_extent{0, 0, frame->display_width, frame->display_height};
    sl::ResourceTag tags[6]{};
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
    const bool valid_hudless = hudless.width == frame->display_width
        && hudless.height == frame->display_height;
    if (g_state.fg_enabled && valid_hudless)
        tags[tag_count++] = sl::ResourceTag(&hudless, sl::kBufferTypeHUDLessColor,
            sl::ResourceLifecycle::eValidUntilPresent, &display_extent);
    const bool valid_ui = frame->ui_color.width == frame->display_width
        && frame->ui_color.height == frame->display_height;
    if (g_state.fg_enabled && valid_hudless && valid_ui) {
        tags[tag_count++] = sl::ResourceTag(&ui, sl::kBufferTypeUIColorAndAlpha,
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
    if (!g_state.first_frame_logged) {
        std::fprintf(stderr, "STREAMLINE: frame contract SR=%s FG=%s Reflex=%s PCL=%s mask=%d\n",
            g_state.sr_enabled ? "on" : "off", g_state.fg_enabled ? "on" : "off",
            g_state.reflex_enabled ? "on" : "off", g_state.pcl_enabled ? "on" : "off",
            result_mask);
        g_state.first_frame_logged = true;
    }
    return result_mask;
}

extern "C" void DethraceStreamlineSetMarker(int marker) {
    if (!g_state.initialized || !g_state.pcl_enabled || g_state.pcl_set_marker == nullptr
        || g_state.frame_token == nullptr)
        return;
    sl::PCLMarker pcl_marker;
    switch (marker) {
    case 2: pcl_marker = sl::PCLMarker::eRenderSubmitStart; break;
    case 3: pcl_marker = sl::PCLMarker::eRenderSubmitEnd; break;
    case 4: pcl_marker = sl::PCLMarker::ePresentStart; break;
    case 5: pcl_marker = sl::PCLMarker::ePresentEnd; break;
    default: return;
    }
    log_result("slPCLSetMarker", g_state.pcl_set_marker(pcl_marker, *g_state.frame_token));
    if (marker == 5) {
        g_state.frame_active = false;
        g_state.simulation_active = false;
        g_state.frame_token = nullptr;
    }
}

extern "C" void DethraceStreamlineSetFrameGenerationActive(int active,
    uint32_t render_width, uint32_t render_height, uint32_t display_width,
    uint32_t display_height) {
    if (!g_state.initialized || !g_state.fg_enabled || g_state.dlssg_set_options == nullptr
        || g_state.fg_runtime_on == (active != 0))
        return;
    if (!g_state.fg_options_set) {
        g_state.render_width = render_width;
        g_state.render_height = render_height;
        g_state.output_width = display_width;
        g_state.output_height = display_height;
        g_state.fg_options_set = true;
    }
    sl::DLSSGOptions options{};
    options.mode = active ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    options.numFramesToGenerate = 1;
    log_result("slDLSSGSetOptions(frame active)", g_state.dlssg_set_options(g_state.viewport, options));
    g_state.fg_runtime_on = active != 0;
    if (g_state.fg_runtime_on)
        query_dlssg_state(false);
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
extern "C" int DethraceStreamlineCreateWin32Surface(void*, void*, void*, void**) { return 0; }
extern "C" int DethraceStreamlineSetVulkanPhysicalDevice(void*) { return 0; }
extern "C" int DethraceStreamlineBeginFrame(void) { return 0; }
extern "C" void DethraceStreamlineEndSimulation(void) {}
extern "C" int DethraceStreamlineEvaluate(const dethrace_streamline_frame*) { return 0; }
extern "C" void DethraceStreamlineSetMarker(int) {}
extern "C" void DethraceStreamlineSetFrameGenerationActive(int, uint32_t, uint32_t,
    uint32_t, uint32_t) {}
extern "C" void DethraceStreamlineShutdown(void) {}

#endif
