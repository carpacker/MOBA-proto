#include "render/renderer.h"
#include "render/pipeline_cache_check.h"
#include "vk/vk.h"
#include "platform/platform.h"          // platform_log / platform_fatal / file I/O / arena
#include "platform/platform_vulkan.h"   // platform_vk_get_loader / platform_vk_create_surface
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

// M2.1: first graphics pipeline. The frame is now rendered with DYNAMIC RENDERING +
// SYNCHRONIZATION2 (both core 1.3, required by ADR-0012): barrier2 to
// COLOR_ATTACHMENT_OPTIMAL -> vkCmdBeginRendering (loadOp=CLEAR carries the animated
// color) -> draw the registry's pipelines -> barrier2 to PRESENT_SRC -> QueueSubmit2.
// Frame pacing is unchanged from M2.0: frames-in-flight = 2, per-frame command buffer
// + image_available semaphore + in_flight fence, per-SWAPCHAIN-IMAGE render_finished
// semaphore + images_in_flight fence pointer.
#define FRAMES_IN_FLIGHT 2
#define MAX_SC_IMAGES    8

// ---- Static pipeline registry (roadmap M2.1) ----------------------------------
// Pipelines the renderer owns, created at startup from offline-compiled SPIR-V
// (ADR-0008: glslc -> ${build}/shaders, located via MOBA_SHADER_DIR). Grows with the
// engine; M2.1 has exactly one entry. All entries share one (empty) pipeline layout
// until descriptors arrive in M2.2.
struct PipelineDesc {
    const char* name;
    const char* vert_spv;    // file name inside MOBA_SHADER_DIR
    const char* frag_spv;
};
enum { PIPELINE_TRIANGLE = 0, PIPELINE_COUNT = 1 };
static const PipelineDesc k_pipeline_registry[PIPELINE_COUNT] = {
    { "triangle", "triangle.vert.spv", "triangle.frag.spv" },
};

// On-disk pipeline cache (DoD: appears after first run, primes the second). Lives in
// the working directory until the asset/user-dir story lands (Phase 4).
// The size bound keeps a corrupt/foreign file from ever reaching the renderer arena's
// hard-abort overrun (the file is untrusted input; the arena policy is for budgets).
// Comfortably under the 16 MiB arena; today's real blobs are KB-scale.
static const char*  k_pipeline_cache_path = "vk_pipeline_cache.bin";
static const size_t k_pipeline_cache_max  = 8u * 1024 * 1024;

struct Renderer {
    Vk                       vk;
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         phys;
    VkPhysicalDeviceProperties props;        // kept: cache-blob validation + logs
    uint32_t                 gfx_family, present_family;
    VkDevice                 device;
    VkQueue                  gfx_queue, present_queue;

    // swapchain (recreated on resize)
    VkSwapchainKHR swapchain;
    VkFormat       sc_format;
    VkExtent2D     sc_extent;
    uint32_t       sc_count;
    VkImage        sc_images[MAX_SC_IMAGES];
    VkImageView    sc_views[MAX_SC_IMAGES];          // color-attachment views (M2.1)
    VkSemaphore    render_finished[MAX_SC_IMAGES];   // per image
    VkFence        images_in_flight[MAX_SC_IMAGES];  // borrowed frame fence, or NULL
    bool           need_recreate;
    bool           sc_can_transfer_src;              // capture/readback possible

    // pipelines (M2.1)
    VkPipelineCache  pipeline_cache;
    VkPipelineLayout pipeline_layout;                // shared, empty until M2.2
    VkPipeline       pipelines[PIPELINE_COUNT];
    VkFormat         pipelines_format;               // sc format they were built for

    // per-frame
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd[FRAMES_IN_FLIGHT];
    VkSemaphore     image_available[FRAMES_IN_FLIGHT];
    VkFence         in_flight[FRAMES_IN_FLIGHT];
    uint32_t        frame;
    uint64_t        frame_count;

    Arena           arena;    // transient: .spv blobs, cache load/save (temp_begin/end)
    PlatformWindow* window;
};

// Validation: log WARN/ERROR, never abort the call. Perf/best-practices noise is kept
// out by the messenger's severity mask.
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)types; (void)user;
    const char* msg = (data && data->pMessage) ? data->pMessage : "";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)        platform_log("[vk ERROR] %s\n", msg);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) platform_log("[vk WARN]  %s\n", msg);
    return VK_FALSE;
}

static bool has_layer(Vk* vk, const char* want) {
    uint32_t n = 0; vk->EnumerateInstanceLayerProperties(&n, nullptr);
    VkLayerProperties* props = (VkLayerProperties*)calloc(n ? n : 1, sizeof(VkLayerProperties));
    if (!props) return false;
    vk->EnumerateInstanceLayerProperties(&n, props);
    bool found = false;
    for (uint32_t i = 0; i < n; ++i) if (strcmp(props[i].layerName, want) == 0) { found = true; break; }
    free(props);
    return found;
}

static int device_type_score(VkPhysicalDeviceType t) {
    if (t == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   return 1000;
    if (t == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) return 100;
    return 10;
}

// ADR-0012 minimum spec: Vulkan 1.3 with dynamicRendering + synchronization2. A
// device below this is skipped during selection (clean "no device" error, no render-
// pass fallback path).
static bool device_meets_min_spec(Vk* vk, VkPhysicalDevice pd, const VkPhysicalDeviceProperties* props) {
    if (props->apiVersion < VK_API_VERSION_1_3) return false;
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    vk->GetPhysicalDeviceFeatures2(pd, &f2);
    return f13.dynamicRendering == VK_TRUE && f13.synchronization2 == VK_TRUE;
}

// Find graphics + present queue families for `pd`/`surface`. Prefers one family that
// does both. Returns false if either is missing.
static bool pick_families(Vk* vk, VkPhysicalDevice pd, VkSurfaceKHR surface, uint32_t* gfx, uint32_t* present) {
    uint32_t qn = 0; vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    VkQueueFamilyProperties* qf = (VkQueueFamilyProperties*)calloc(qn ? qn : 1, sizeof(VkQueueFamilyProperties));
    if (!qf) return false;
    vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
    const uint32_t NONE = 0xffffffffu;
    uint32_t g = NONE, p = NONE;
    for (uint32_t i = 0; i < qn; ++i) {
        bool can_gfx = (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        VkBool32 can_present = VK_FALSE;
        vk->GetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &can_present);
        if (can_gfx && can_present) { g = i; p = i; break; }   // both in one family — ideal
        if (can_gfx && g == NONE) g = i;
        if (can_present && p == NONE) p = i;
    }
    free(qf);
    if (g == NONE || p == NONE) return false;
    *gfx = g; *present = p;
    return true;
}

// ---- Pipelines (M2.1) ----------------------------------------------------------

// Load one offline-compiled .spv from MOBA_SHADER_DIR into a shader module. The blob
// lives only inside the caller's TempMemory scope.
static VkShaderModule load_shader_module(Renderer* r, const char* spv_name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", MOBA_SHADER_DIR, spv_name);

    PlatformFile f;
    if (!platform_file_read(path, arena_allocator(&r->arena), &f)) {
        platform_log("renderer: shader missing: %s (build the 'shaders' target?)\n", path);
        return VK_NULL_HANDLE;
    }
    if (f.size == 0 || (f.size % 4) != 0) {
        platform_log("renderer: shader %s is not valid SPIR-V (%zu bytes)\n", path, f.size);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = f.size;
    ci.pCode    = (const uint32_t*)f.data;   // 16-aligned by platform_file_read
    VkShaderModule mod = VK_NULL_HANDLE;
    if (r->vk.CreateShaderModule(r->device, &ci, nullptr, &mod) != VK_SUCCESS) {
        platform_log("renderer: vkCreateShaderModule failed for %s\n", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

static void destroy_pipelines(Renderer* r) {
    for (int i = 0; i < PIPELINE_COUNT; ++i)
        if (r->pipelines[i]) { r->vk.DestroyPipeline(r->device, r->pipelines[i], nullptr); r->pipelines[i] = VK_NULL_HANDLE; }
}

// Build every registry pipeline against the CURRENT swapchain format, through the
// pipeline cache. Viewport/scissor are dynamic state, so a resize never rebuilds —
// only a (rare) surface-format change does.
static bool build_pipelines(Renderer* r) {
    for (int i = 0; i < PIPELINE_COUNT; ++i) {
        const PipelineDesc* d = &k_pipeline_registry[i];
        TempMemory tm = temp_begin(&r->arena);
        VkShaderModule vert = load_shader_module(r, d->vert_spv);
        VkShaderModule frag = load_shader_module(r, d->frag_spv);
        temp_end(tm);   // modules own a copy of the code; the blobs can go
        if (!vert || !frag) {
            if (vert) r->vk.DestroyShaderModule(r->device, vert, nullptr);
            if (frag) r->vk.DestroyShaderModule(r->device, frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName  = "main";

        // M2.1: vertices are hardcoded in the shader — empty vertex input on purpose
        // (roadmap: "keep it hardcoded here"; real buffers arrive in M2.2).
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;     // pointers null: dynamic state below
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;    // first triangle: visible no matter the winding
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blend_att{};
        blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blend_att;

        VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dyn_states;

        // Dynamic rendering: the pipeline declares attachment formats here instead of
        // referencing a VkRenderPass (ADR-0012; no render-pass objects in this engine).
        VkPipelineRenderingCreateInfo rendering{};
        rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount    = 1;
        rendering.pColorAttachmentFormats = &r->sc_format;

        VkGraphicsPipelineCreateInfo ci{};
        ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.pNext               = &rendering;
        ci.stageCount          = 2;
        ci.pStages             = stages;
        ci.pVertexInputState   = &vin;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState      = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState   = &ms;
        ci.pColorBlendState    = &blend;
        ci.pDynamicState       = &dyn;
        ci.layout              = r->pipeline_layout;
        ci.renderPass          = VK_NULL_HANDLE;

        VkResult res = r->vk.CreateGraphicsPipelines(r->device, r->pipeline_cache, 1, &ci, nullptr, &r->pipelines[i]);
        r->vk.DestroyShaderModule(r->device, vert, nullptr);
        r->vk.DestroyShaderModule(r->device, frag, nullptr);
        if (res != VK_SUCCESS) {
            platform_log("renderer: vkCreateGraphicsPipelines failed for '%s' (%d)\n", d->name, (int)res);
            return false;
        }
    }
    r->pipelines_format = r->sc_format;
    return true;
}

// Load the on-disk cache if it is OURS (vendor/device/UUID validated — a stale or
// foreign blob is dropped and rebuilt, never fed to the driver), then create the
// VkPipelineCache, possibly primed.
static void create_pipeline_cache(Renderer* r) {
    VkPipelineCacheCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    TempMemory tm = temp_begin(&r->arena);
    PlatformFile f;
    size_t disk_size = 0;
    if (!platform_file_size(k_pipeline_cache_path, &disk_size)) {
        platform_log("renderer: no pipeline cache on disk — starting empty\n");
    } else if (disk_size > k_pipeline_cache_max) {
        // Size-check BEFORE the read: an oversized file would hard-abort in the fixed
        // arena, which is the budget policy's job, never an on-disk file's.
        platform_log("renderer: pipeline cache %s is oversized (%zu bytes) — starting empty\n", k_pipeline_cache_path, disk_size);
    } else if (platform_file_read(k_pipeline_cache_path, arena_allocator(&r->arena), &f)) {
        if (pipeline_cache_blob_ok(f.data, f.size, r->props.vendorID, r->props.deviceID,
                                   r->props.pipelineCacheUUID)) {
            ci.initialDataSize = f.size;
            ci.pInitialData    = f.data;
            platform_log("renderer: pipeline cache primed from %s (%zu bytes)\n", k_pipeline_cache_path, f.size);
        } else {
            platform_log("renderer: pipeline cache %s is stale/foreign — starting empty\n", k_pipeline_cache_path);
        }
    } else {
        platform_log("renderer: pipeline cache %s unreadable — starting empty\n", k_pipeline_cache_path);
    }
    if (r->vk.CreatePipelineCache(r->device, &ci, nullptr, &r->pipeline_cache) != VK_SUCCESS) {
        // Non-fatal: pipelines build fine without a cache, just slower.
        platform_log("renderer: vkCreatePipelineCache failed — continuing without\n");
        r->pipeline_cache = VK_NULL_HANDLE;
    }
    temp_end(tm);
}

static void save_pipeline_cache(Renderer* r) {
    if (!r->pipeline_cache) return;
    size_t n = 0;
    if (r->vk.GetPipelineCacheData(r->device, r->pipeline_cache, &n, nullptr) != VK_SUCCESS || n == 0) return;
    if (n > k_pipeline_cache_max) {   // driver-reported size: same bound as the load
        platform_log("renderer: pipeline cache too large to save (%zu bytes) — skipped\n", n);
        return;
    }
    TempMemory tm = temp_begin(&r->arena);
    void* data = arena_push(&r->arena, n, 16);
    if (r->vk.GetPipelineCacheData(r->device, r->pipeline_cache, &n, data) == VK_SUCCESS &&
        platform_file_write(k_pipeline_cache_path, data, n)) {
        platform_log("renderer: pipeline cache saved to %s (%zu bytes)\n", k_pipeline_cache_path, n);
    } else {
        platform_log("renderer: pipeline cache save failed (non-fatal)\n");
    }
    temp_end(tm);
}

// ---- Swapchain -------------------------------------------------------------------

static void destroy_swapchain(Renderer* r) {
    for (uint32_t i = 0; i < r->sc_count; ++i) {
        if (r->sc_views[i])        { r->vk.DestroyImageView(r->device, r->sc_views[i], nullptr); r->sc_views[i] = VK_NULL_HANDLE; }
        if (r->render_finished[i]) { r->vk.DestroySemaphore(r->device, r->render_finished[i], nullptr); r->render_finished[i] = VK_NULL_HANDLE; }
    }
    if (r->swapchain) { r->vk.DestroySwapchainKHR(r->device, r->swapchain, nullptr); r->swapchain = VK_NULL_HANDLE; }
    r->sc_count = 0;
}

// (Re)create the swapchain for the given framebuffer size. Caller has made the device
// idle. Returns false on a transient 0-extent (minimized) — caller should skip drawing.
static bool create_swapchain(Renderer* r, uint32_t fb_w, uint32_t fb_h) {
    VkSurfaceCapabilitiesKHR caps{};
    r->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(r->phys, r->surface, &caps);

    VkExtent2D extent;
    if (caps.currentExtent.width != 0xffffffffu) {
        extent = caps.currentExtent;
    } else {
        extent.width  = fb_w < caps.minImageExtent.width  ? caps.minImageExtent.width  : (fb_w > caps.maxImageExtent.width  ? caps.maxImageExtent.width  : fb_w);
        extent.height = fb_h < caps.minImageExtent.height ? caps.minImageExtent.height : (fb_h > caps.maxImageExtent.height ? caps.maxImageExtent.height : fb_h);
    }
    if (extent.width == 0 || extent.height == 0) return false;   // minimized

    // Format: prefer B8G8R8A8_SRGB / SRGB_NONLINEAR, else the first reported.
    uint32_t fn = 0; r->vk.GetPhysicalDeviceSurfaceFormatsKHR(r->phys, r->surface, &fn, nullptr);
    VkSurfaceFormatKHR* fmts = (VkSurfaceFormatKHR*)calloc(fn ? fn : 1, sizeof(VkSurfaceFormatKHR));
    r->vk.GetPhysicalDeviceSurfaceFormatsKHR(r->phys, r->surface, &fn, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < fn; ++i)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = fmts[i]; break; }
    free(fmts);

    // Present mode: prefer MAILBOX (not guaranteed), else FIFO (always available).
    uint32_t pn = 0; r->vk.GetPhysicalDeviceSurfacePresentModesKHR(r->phys, r->surface, &pn, nullptr);
    VkPresentModeKHR* pmodes = (VkPresentModeKHR*)calloc(pn ? pn : 1, sizeof(VkPresentModeKHR));
    r->vk.GetPhysicalDeviceSurfacePresentModesKHR(r->phys, r->surface, &pn, pmodes);
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < pn; ++i) if (pmodes[i] == VK_PRESENT_MODE_MAILBOX_KHR) { present_mode = VK_PRESENT_MODE_MAILBOX_KHR; break; }
    free(pmodes);

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && want > caps.maxImageCount) want = caps.maxImageCount;

    // M2.1 renders into the image (COLOR_ATTACHMENT, always supported for swapchains);
    // TRANSFER_SRC is added when the surface allows it so renderer_capture can read
    // pixels back. The M2.0 TRANSFER_DST clear usage is gone — loadOp clears now.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    r->sc_can_transfer_src = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (r->sc_can_transfer_src) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = r->surface;
    ci.minImageCount    = want;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = usage;
    uint32_t fams[2] = { r->gfx_family, r->present_family };
    if (r->gfx_family != r->present_family) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = fams;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = present_mode;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE;

    if (r->vk.CreateSwapchainKHR(r->device, &ci, nullptr, &r->swapchain) != VK_SUCCESS) {
        platform_log("renderer: vkCreateSwapchainKHR failed\n"); return false;
    }
    r->sc_format = chosen.format;
    r->sc_extent = extent;

    uint32_t cnt = 0; r->vk.GetSwapchainImagesKHR(r->device, r->swapchain, &cnt, nullptr);
    if (cnt > MAX_SC_IMAGES) platform_fatal("renderer: swapchain image count %u > MAX_SC_IMAGES\n", cnt);
    r->vk.GetSwapchainImagesKHR(r->device, r->swapchain, &cnt, r->sc_images);
    r->sc_count = cnt;

    VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < cnt; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = r->sc_images[i];
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = r->sc_format;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.layerCount     = 1;
        if (r->vk.CreateImageView(r->device, &vci, nullptr, &r->sc_views[i]) != VK_SUCCESS)
            platform_fatal("renderer: vkCreateImageView failed for swapchain image %u\n", i);
        r->vk.CreateSemaphore(r->device, &sci, nullptr, &r->render_finished[i]);
        r->images_in_flight[i] = VK_NULL_HANDLE;
    }
    return true;
}

static void recreate_swapchain(Renderer* r, uint32_t fb_w, uint32_t fb_h) {
    r->vk.DeviceWaitIdle(r->device);   // Phase-1 brute force (ARCHITECTURE / roadmap M2.0)
    destroy_swapchain(r);
    create_swapchain(r, fb_w, fb_h);
    r->need_recreate = false;

    // Pipelines bake the color-attachment format. A recreate normally keeps the same
    // surface format, but if it ever changes (driver/monitor oddity), rebuild.
    if (r->swapchain && r->pipelines[0] && r->pipelines_format != r->sc_format) {
        platform_log("renderer: swapchain format changed (%d -> %d) — rebuilding pipelines\n",
                     (int)r->pipelines_format, (int)r->sc_format);
        destroy_pipelines(r);
        if (!build_pipelines(r))
            platform_fatal("renderer: pipeline rebuild after format change failed\n");
    }
}

// ---- Create / destroy --------------------------------------------------------------

Renderer* renderer_create(PlatformWindow* window) {
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)platform_vk_get_loader();
    if (!gipa) { platform_log("renderer: no Vulkan loader (vulkan-1.dll missing?)\n"); return nullptr; }

    Renderer* r = (Renderer*)calloc(1, sizeof(Renderer));
    if (!r) return nullptr;
    r->window = window;
    if (!vk_load_global(&r->vk, gipa)) { free(r); return nullptr; }

    if (!platform_arena_reserve(&r->arena, 16u * 1024 * 1024)) { free(r); return nullptr; }

    const bool validation = has_layer(&r->vk, "VK_LAYER_KHRONOS_validation");
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char* exts[]   = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

    VkApplicationInfo app{};
    app.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "moba";
    app.apiVersion       = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    if (validation) { ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = layers; }

    if (r->vk.CreateInstance(&ci, nullptr, &r->instance) != VK_SUCCESS) {
        platform_log("renderer: vkCreateInstance failed\n");
        platform_arena_release(&r->arena);
        free(r);
        return nullptr;
    }
    vk_load_instance(&r->vk, r->instance);

    if (validation && r->vk.CreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT dci{};
        dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debug_cb;
        r->vk.CreateDebugUtilsMessengerEXT(r->instance, &dci, nullptr, &r->debug);
    }

    // Surface (HWND stays in platform, ADR-0005).
    unsigned long long surf = 0;
    if (!platform_vk_create_surface(window, r->instance, &surf)) { platform_log("renderer: surface creation failed\n"); renderer_destroy(r); return nullptr; }
    r->surface = (VkSurfaceKHR)(uintptr_t)surf;

    // Pick the best physical device that has graphics+present, the swapchain ext, and
    // meets the ADR-0012 minimum spec.
    uint32_t pdn = 0; r->vk.EnumeratePhysicalDevices(r->instance, &pdn, nullptr);
    if (pdn == 0) { platform_log("renderer: no Vulkan physical devices\n"); renderer_destroy(r); return nullptr; }
    VkPhysicalDevice* pds = (VkPhysicalDevice*)calloc(pdn, sizeof(VkPhysicalDevice));
    if (!pds) { renderer_destroy(r); return nullptr; }
    r->vk.EnumeratePhysicalDevices(r->instance, &pdn, pds);

    int best = -1;
    for (uint32_t i = 0; i < pdn; ++i) {
        uint32_t g, p;
        VkPhysicalDeviceProperties props{};
        r->vk.GetPhysicalDeviceProperties(pds[i], &props);
        if (!device_meets_min_spec(&r->vk, pds[i], &props)) {
            platform_log("renderer: skipping %s — below minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2)\n", props.deviceName);
            continue;
        }
        if (!pick_families(&r->vk, pds[i], r->surface, &g, &p)) continue;
        int s = device_type_score(props.deviceType);
        if (s > best) { best = s; r->phys = pds[i]; r->gfx_family = g; r->present_family = p; r->props = props; }
    }
    free(pds);
    if (best < 0) {
        platform_log("renderer: no device meets the minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2 + graphics/present) — ADR-0012\n");
        renderer_destroy(r);
        return nullptr;
    }

    // Logical device + queues (VK_KHR_swapchain). Up to two distinct queue families.
    // The 1.3 features the engine is built on are enabled here (gated above).
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qcis[2]{}; uint32_t qc = 0;
    qcis[qc].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qcis[qc].queueFamilyIndex = r->gfx_family; qcis[qc].queueCount = 1; qcis[qc].pQueuePriorities = &prio; ++qc;
    if (r->present_family != r->gfx_family) {
        qcis[qc].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qcis[qc].queueFamilyIndex = r->present_family; qcis[qc].queueCount = 1; qcis[qc].pQueuePriorities = &prio; ++qc;
    }
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    const char* dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &f13;
    dci.queueCreateInfoCount    = qc;
    dci.pQueueCreateInfos       = qcis;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    if (r->vk.CreateDevice(r->phys, &dci, nullptr, &r->device) != VK_SUCCESS) {
        platform_log("renderer: vkCreateDevice failed\n"); renderer_destroy(r); return nullptr;
    }
    vk_load_device(&r->vk, r->device);
    r->vk.GetDeviceQueue(r->device, r->gfx_family, 0, &r->gfx_queue);
    r->vk.GetDeviceQueue(r->device, r->present_family, 0, &r->present_queue);

    // Per-frame command buffers + sync.
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = r->gfx_family;
    r->vk.CreateCommandPool(r->device, &pci, nullptr, &r->cmd_pool);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = r->cmd_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = FRAMES_IN_FLIGHT;
    r->vk.AllocateCommandBuffers(r->device, &ai, r->cmd);

    VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        r->vk.CreateSemaphore(r->device, &sci, nullptr, &r->image_available[i]);
        r->vk.CreateFence(r->device, &fci, nullptr, &r->in_flight[i]);
    }

    // Pipeline layout (empty until descriptors, M2.2) + cache + first swapchain.
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (r->vk.CreatePipelineLayout(r->device, &plci, nullptr, &r->pipeline_layout) != VK_SUCCESS) {
        platform_log("renderer: vkCreatePipelineLayout failed\n"); renderer_destroy(r); return nullptr;
    }
    create_pipeline_cache(r);

    int32_t w = 0, h = 0; platform_window_size(window, &w, &h);
    create_swapchain(r, (uint32_t)(w > 0 ? w : 1), (uint32_t)(h > 0 ? h : 1));

    // Pipelines need the swapchain format; if the window started minimized the build
    // is deferred to the first successful (re)create inside renderer_draw.
    if (r->swapchain && !build_pipelines(r)) {
        platform_log("renderer: pipeline build failed — no renderer\n");
        renderer_destroy(r);
        return nullptr;
    }

    const char* kind = r->props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   ? "discrete"
                     : r->props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated" : "other";
    platform_log("renderer: Vulkan 1.3 up | validation=%s | GPU: %s (%s) | swapchain %ux%u x%u | %d pipeline(s)\n",
                 validation ? "on" : "off", r->props.deviceName, kind,
                 r->sc_extent.width, r->sc_extent.height, r->sc_count, (int)PIPELINE_COUNT);
    return r;
}

void renderer_destroy(Renderer* r) {
    if (!r) return;
    if (r->device) r->vk.DeviceWaitIdle(r->device);
    if (r->device) save_pipeline_cache(r);   // before anything GPU-side is torn down
    destroy_pipelines(r);
    if (r->pipeline_layout) r->vk.DestroyPipelineLayout(r->device, r->pipeline_layout, nullptr);
    if (r->pipeline_cache)  r->vk.DestroyPipelineCache(r->device, r->pipeline_cache, nullptr);
    destroy_swapchain(r);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (r->image_available[i]) r->vk.DestroySemaphore(r->device, r->image_available[i], nullptr);
        if (r->in_flight[i])       r->vk.DestroyFence(r->device, r->in_flight[i], nullptr);
    }
    if (r->cmd_pool) r->vk.DestroyCommandPool(r->device, r->cmd_pool, nullptr);
    if (r->device)   r->vk.DestroyDevice(r->device, nullptr);
    if (r->surface)  r->vk.DestroySurfaceKHR(r->instance, r->surface, nullptr);
    if (r->debug && r->vk.DestroyDebugUtilsMessengerEXT) r->vk.DestroyDebugUtilsMessengerEXT(r->instance, r->debug, nullptr);
    if (r->instance) r->vk.DestroyInstance(r->instance, nullptr);
    platform_arena_release(&r->arena);
    free(r);
}

// ---- Frame -------------------------------------------------------------------------

// sync2 image barrier helper: one full-subresource color transition.
static void image_barrier2(Renderer* r, VkCommandBuffer cb, VkImage image,
                           VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                           VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier2 b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask        = src_stage;
    b.srcAccessMask       = src_access;
    b.dstStageMask        = dst_stage;
    b.dstAccessMask       = dst_access;
    b.oldLayout           = old_layout;
    b.newLayout           = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &b;
    r->vk.CmdPipelineBarrier2(cb, &dep);
}

// Pending readback for the frame being recorded (renderer_capture's slow path).
struct CaptureState {
    VkBuffer       buffer;
    VkDeviceMemory memory;
    uint32_t       width, height;
    int            waited_frame;   // frame slot whose fence guards the copy, or -1
};

// Render + present one frame. When `cap` is non-null and the swapchain supports
// TRANSFER_SRC, the frame is also copied into a fresh host-visible buffer recorded in
// `cap` (caller waits the frame fence, maps, converts, frees). Returns false if no
// frame was submitted (minimized, transient acquire failure, swapchain gone).
static bool draw_frame(Renderer* r, int fb_width, int fb_height, bool minimized, CaptureState* cap) {
    if (cap) { cap->buffer = VK_NULL_HANDLE; cap->memory = VK_NULL_HANDLE; cap->waited_frame = -1; }
    if (!r || minimized || fb_width <= 0 || fb_height <= 0) return false;
    uint32_t fw = (uint32_t)fb_width, fh = (uint32_t)fb_height;

    if (r->need_recreate || r->swapchain == VK_NULL_HANDLE || fw != r->sc_extent.width || fh != r->sc_extent.height) {
        recreate_swapchain(r, fw, fh);
        if (r->swapchain == VK_NULL_HANDLE) return false;   // still minimized
    }
    // Deferred first build (window opened minimized) — needs the swapchain format.
    if (!r->pipelines[0] && !build_pipelines(r))
        platform_fatal("renderer: deferred pipeline build failed\n");

    uint32_t fr = r->frame;
    r->vk.WaitForFences(r->device, 1, &r->in_flight[fr], VK_TRUE, UINT64_MAX);

    uint32_t img = 0;
    VkResult acq = r->vk.AcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->image_available[fr], VK_NULL_HANDLE, &img);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { r->need_recreate = true; return false; }   // no image -> semaphore NOT signaled, safe to bail
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) { platform_log("renderer: acquire failed (%d)\n", (int)acq); return false; }

    // Don't write an image a prior frame is still presenting.
    if (r->images_in_flight[img] != VK_NULL_HANDLE)
        r->vk.WaitForFences(r->device, 1, &r->images_in_flight[img], VK_TRUE, UINT64_MAX);
    r->images_in_flight[img] = r->in_flight[fr];
    r->vk.ResetFences(r->device, 1, &r->in_flight[fr]);

    // Optional readback buffer for this frame (created at the now-final extent).
    bool capture = false;
    if (cap && r->sc_can_transfer_src) {
        VkDeviceSize bytes = (VkDeviceSize)r->sc_extent.width * r->sc_extent.height * 4;
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = bytes;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (r->vk.CreateBuffer(r->device, &bci, nullptr, &cap->buffer) == VK_SUCCESS) {
            VkMemoryRequirements req{};
            r->vk.GetBufferMemoryRequirements(r->device, cap->buffer, &req);
            VkPhysicalDeviceMemoryProperties mp{};
            r->vk.GetPhysicalDeviceMemoryProperties(r->phys, &mp);
            const VkMemoryPropertyFlags want_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            uint32_t type = UINT32_MAX;
            for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
                if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want_flags) == want_flags) { type = i; break; }
            VkMemoryAllocateInfo mai{};
            mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mai.allocationSize  = req.size;
            mai.memoryTypeIndex = type;
            if (type != UINT32_MAX &&
                r->vk.AllocateMemory(r->device, &mai, nullptr, &cap->memory) == VK_SUCCESS &&
                r->vk.BindBufferMemory(r->device, cap->buffer, cap->memory, 0) == VK_SUCCESS) {
                cap->width  = r->sc_extent.width;
                cap->height = r->sc_extent.height;
                capture = true;
            }
        }
        if (!capture) {   // fall back to a plain frame; caller sees waited_frame == -1
            platform_log("renderer: capture buffer setup failed — drawing without readback\n");
            if (cap->memory) { r->vk.FreeMemory(r->device, cap->memory, nullptr); cap->memory = VK_NULL_HANDLE; }
            if (cap->buffer) { r->vk.DestroyBuffer(r->device, cap->buffer, nullptr); cap->buffer = VK_NULL_HANDLE; }
        }
    }

    // Animated clear color (presentation only — float is fine here).
    double t = (double)r->frame_count * 0.02;
    VkClearValue clear{};
    clear.color.float32[0] = (float)(0.5 + 0.5 * sin(t));
    clear.color.float32[1] = (float)(0.5 + 0.5 * sin(t + 2.094));
    clear.color.float32[2] = (float)(0.5 + 0.5 * sin(t + 4.188));
    clear.color.float32[3] = 1.0f;

    VkCommandBuffer cb = r->cmd[fr];
    r->vk.ResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r->vk.BeginCommandBuffer(cb, &bi);

    // UNDEFINED -> COLOR_ATTACHMENT. src stage = COLOR_ATTACHMENT_OUTPUT chains the
    // transition after the image_available semaphore wait (which waits at that stage).
    image_barrier2(r, cb, r->sc_images[img],
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo color{};
    color.sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView        = r->sc_views[img];
    color.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;     // the M2.0 clear lives on as loadOp
    color.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue       = clear;

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent    = r->sc_extent;
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &color;
    r->vk.CmdBeginRendering(cb, &ri);

    VkViewport vp{};
    vp.width    = (float)r->sc_extent.width;
    vp.height   = (float)r->sc_extent.height;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = r->sc_extent;
    r->vk.CmdSetViewport(cb, 0, 1, &vp);
    r->vk.CmdSetScissor(cb, 0, 1, &scissor);

    r->vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[PIPELINE_TRIANGLE]);
    r->vk.CmdDraw(cb, 3, 1, 0, 0);   // 3 verts hardcoded in triangle.vert

    r->vk.CmdEndRendering(cb);

    if (capture) {
        // COLOR_ATTACHMENT -> TRANSFER_SRC, copy to the host buffer, then -> PRESENT.
        image_barrier2(r, cb, r->sc_images[img],
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width  = r->sc_extent.width;
        region.imageExtent.height = r->sc_extent.height;
        region.imageExtent.depth  = 1;
        r->vk.CmdCopyImageToBuffer(cb, r->sc_images[img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cap->buffer, 1, &region);
        image_barrier2(r, cb, r->sc_images[img],
                       VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    } else {
        // COLOR_ATTACHMENT -> PRESENT. Visibility to the presentation engine comes
        // from the render_finished semaphore, hence dst NONE (sync2 allows it).
        image_barrier2(r, cb, r->sc_images[img],
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    r->vk.EndCommandBuffer(cb);

    VkSemaphoreSubmitInfo wait{};
    wait.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = r->image_available[fr];
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{};
    signal.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = r->render_finished[img];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{};
    cbi.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbi.commandBuffer = cb;
    VkSubmitInfo2 si{};
    si.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.waitSemaphoreInfoCount   = 1;
    si.pWaitSemaphoreInfos      = &wait;
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos    = &signal;
    r->vk.QueueSubmit2(r->gfx_queue, 1, &si, r->in_flight[fr]);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &r->render_finished[img];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &r->swapchain;
    pi.pImageIndices      = &img;
    VkResult pres = r->vk.QueuePresentKHR(r->present_queue, &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) r->need_recreate = true;
    else if (pres != VK_SUCCESS) platform_log("renderer: present failed (%d)\n", (int)pres);

    if (capture) cap->waited_frame = (int)fr;
    r->frame = (fr + 1) % FRAMES_IN_FLIGHT;
    ++r->frame_count;
    return true;
}

void renderer_draw(Renderer* r, int fb_width, int fb_height, bool minimized) {
    draw_frame(r, fb_width, fb_height, minimized, nullptr);
}

bool renderer_capture(Renderer* r, int fb_width, int fb_height, bool minimized,
                      Allocator alloc, RendererCapture* out) {
    if (!r || !out) return false;
    if (!r->sc_can_transfer_src) {
        platform_log("renderer: capture unsupported (swapchain has no TRANSFER_SRC)\n");
        return false;
    }
    // Only 32-bit BGRA/RGBA swapchain formats are handled (every format this engine
    // selects; see create_swapchain's preference order).
    const bool bgra = r->sc_format == VK_FORMAT_B8G8R8A8_SRGB || r->sc_format == VK_FORMAT_B8G8R8A8_UNORM;
    const bool rgba = r->sc_format == VK_FORMAT_R8G8B8A8_SRGB || r->sc_format == VK_FORMAT_R8G8B8A8_UNORM;
    if (!bgra && !rgba) {
        platform_log("renderer: capture unsupported for swapchain format %d\n", (int)r->sc_format);
        return false;
    }

    CaptureState cap;
    bool drew = draw_frame(r, fb_width, fb_height, minimized, &cap);
    if (!drew || cap.waited_frame < 0) {
        if (cap.memory) r->vk.FreeMemory(r->device, cap.memory, nullptr);
        if (cap.buffer) r->vk.DestroyBuffer(r->device, cap.buffer, nullptr);
        return false;   // transient (minimized/OUT_OF_DATE) — caller may retry
    }

    // The frame fence covers the copy; HOST_COHERENT makes it visible after the wait.
    r->vk.WaitForFences(r->device, 1, &r->in_flight[cap.waited_frame], VK_TRUE, UINT64_MAX);

    bool ok = false;
    void* mapped = nullptr;
    if (r->vk.MapMemory(r->device, cap.memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS) {
        size_t px_count = (size_t)cap.width * cap.height;
        uint8_t* dst = (uint8_t*)mem_alloc(alloc, px_count * 4, MEM_DEFAULT_ALIGN);
        if (dst) {
            const uint8_t* src = (const uint8_t*)mapped;
            if (rgba) {
                memcpy(dst, src, px_count * 4);
            } else {   // BGRA -> RGBA swizzle
                for (size_t p = 0; p < px_count; ++p) {
                    dst[p * 4 + 0] = src[p * 4 + 2];
                    dst[p * 4 + 1] = src[p * 4 + 1];
                    dst[p * 4 + 2] = src[p * 4 + 0];
                    dst[p * 4 + 3] = src[p * 4 + 3];
                }
            }
            out->width  = (int)cap.width;
            out->height = (int)cap.height;
            out->rgba8  = dst;
            ok = true;
        }
        r->vk.UnmapMemory(r->device, cap.memory);
    }
    r->vk.FreeMemory(r->device, cap.memory, nullptr);
    r->vk.DestroyBuffer(r->device, cap.buffer, nullptr);
    return ok;
}
