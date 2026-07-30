#include "engine/render/vulkan/VulkanRenderBackendInternal.h"
#include "engine/render/vulkan/VulkanSpriteInstanceState.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <stb_image_write.h>

#include "engine/core/Environment.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/vulkan/VulkanEnvironmentParity.h"

#ifndef PAC_VULKAN_SHADER_DIR
#define PAC_VULKAN_SHADER_DIR "generated/vulkan"
#endif

namespace {

[[noreturn]] void throwVk(const char* operation, VkResult result) {
    throw std::runtime_error(
        std::string(operation) + " failed with VkResult " + std::to_string(result) + ".");
}

void requireVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throwVk(operation, result);
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsCaseInsensitive(std::string_view value, std::string_view needle) {
    if (needle.empty()) return true;
    return lowerCopy(std::string(value)).find(lowerCopy(std::string(needle))) != std::string::npos;
}

bool hasDeviceExtension(VkPhysicalDevice device, const char* requested) {
    std::uint32_t count = 0u;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0u &&
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(extensions.begin(), extensions.end(), [requested](const auto& extension) {
        return std::strcmp(extension.extensionName, requested) == 0;
    });
}

struct QueueFamilies {
    std::uint32_t graphics = UINT32_MAX;
    std::uint32_t present = UINT32_MAX;

    bool complete() const {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t count = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    if (count > 0u) vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

    QueueFamilies out;
    for (std::uint32_t i = 0u; i < count; ++i) {
        if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u &&
            out.graphics == UINT32_MAX) {
            out.graphics = i;
        }
        VkBool32 supportsPresent = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supportsPresent) == VK_SUCCESS &&
            supportsPresent == VK_TRUE && out.present == UINT32_MAX) {
            out.present = i;
        }
    }
    return out;
}

bool hasSwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t formatCount = 0u;
    std::uint32_t presentModeCount = 0u;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr) != VK_SUCCESS ||
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &presentModeCount, nullptr) != VK_SUCCESS) {
        return false;
    }
    return formatCount > 0u && presentModeCount > 0u;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    const std::array<VkFormat, 2> preferred{
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
    };
    for (VkFormat format : preferred) {
        const auto it = std::find_if(formats.begin(), formats.end(), [format](const auto& candidate) {
            return candidate.format == format &&
                   candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (it != formats.end()) return *it;
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (vsync) return VK_PRESENT_MODE_FIFO_KHR;
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (VkCompositeAlphaFlagBitsKHR choice : choices) {
        if ((supported & choice) != 0u) return choice;
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkPipeline createGraphicsPipeline(
    VkDevice device,
    VkRenderPass renderPass,
    VkPipelineLayout layout,
    VkShaderModule vertexShader,
    VkShaderModule fragmentShader,
    const VkVertexInputBindingDescription& binding,
    const std::vector<VkVertexInputAttributeDescription>& attributes,
    bool blendEnabled,
    std::uint8_t blendMode,
    bool dualSourceBlend,
    bool depthTestEnabled,
    bool depthWriteEnabled) {
    const VkPipelineShaderStageCreateInfo stages[2]{
        VkPipelineShaderStageCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0u,
            VK_SHADER_STAGE_VERTEX_BIT,
            vertexShader,
            "main",
            nullptr},
        VkPipelineShaderStageCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0u,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            fragmentShader,
            "main",
            nullptr},
    };

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1u;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1u;
    viewportState.scissorCount = 1u;

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = engine::render::parity_contract::kWorldCullEnabled
        ? VK_CULL_MODE_BACK_BIT
        : VK_CULL_MODE_NONE;
    rasterizer.frontFace = engine::render::parity_contract::kWorldFrontFaceClockwise
        ? VK_FRONT_FACE_CLOCKWISE
        : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = engine::render::parity_contract::kWorldDepthFuncLessEqual
        ? VK_COMPARE_OP_LESS_OR_EQUAL
        : VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = blendEnabled ? VK_TRUE : VK_FALSE;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    if (dualSourceBlend) {
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA;
        blendAttachment.dstColorBlendFactor = blendMode == 1u
            ? VK_BLEND_FACTOR_ONE
            : VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (blendMode == 1u) {
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (blendMode == 2u) {
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    } else {
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1u;
    colorBlend.pAttachments = &blendAttachment;

    constexpr std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo createInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    createInfo.stageCount = 2u;
    createInfo.pStages = stages;
    createInfo.pVertexInputState = &vertexInput;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState = &viewportState;
    createInfo.pRasterizationState = &rasterizer;
    createInfo.pMultisampleState = &multisampling;
    createInfo.pDepthStencilState = &depthStencil;
    createInfo.pColorBlendState = &colorBlend;
    createInfo.pDynamicState = &dynamicState;
    createInfo.layout = layout;
    createInfo.renderPass = renderPass;
    createInfo.subpass = 0u;

    VkPipeline pipeline = VK_NULL_HANDLE;
    requireVk(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &createInfo, nullptr, &pipeline),
        "vkCreateGraphicsPipelines");
    return pipeline;
}

} // namespace

void VulkanRenderBackendImpl::initialize(SDL_Window* sdlWindow,
                                         int width,
                                         int height,
                                         bool enableVsync,
                                         const std::string& preferredAdapterName) {
    window = sdlWindow;
    requestedWidth = std::max(1, width);
    requestedHeight = std::max(1, height);
    vsyncEnabled = enableVsync;

    try {
        createInstance();
        if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE) {
            throw std::runtime_error(
                std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }
        selectPhysicalDevice(preferredAdapterName);
        createDevice();
        createCommandResources();
        createDescriptorResources();
        createFrameResources();

        static constexpr unsigned char kWorldFallback[4] = {255u, 255u, 255u, 255u};
        fallbackWorldTexture = createTexture(
            kWorldFallback, 1, 1, true, 33071, 33071, false);
        static constexpr unsigned char kWorldFlatNormal[4] = {128u, 128u, 255u, 255u};
        fallbackWorldNormalTexture = createTexture(
            kWorldFlatNormal, 1, 1, false, 33071, 33071, false);
        fallbackWorldLinearTexture = createTexture(
            kWorldFallback, 1, 1, false, 33071, 33071, false);
        fallbackWorldEmissiveTexture = createTexture(
            kWorldFallback, 1, 1, true, 33071, 33071, false);
        createEnvironmentResources();
        fallbackWorldMaterial = createWorldMaterial(
            fallbackWorldTexture,
            fallbackWorldNormalTexture,
            fallbackWorldLinearTexture,
            fallbackWorldLinearTexture,
            fallbackWorldEmissiveTexture,
            neutralPmremTexture,
            fallbackWorldLinearTexture,
            fallbackWorldLinearTexture);
        if (descriptorIndexingSupported) {
            if (!registerIndexedWorldMaterial(fallbackWorldMaterial)) {
                throw std::runtime_error(
                    "Vulkan indexed world material table could not register its fallback material.");
            }
            initializeIndexedWorldMaterialSets();
        }
        static constexpr unsigned char kSpriteFallback[16] = {
            72u, 90u, 108u, 255u,
            56u, 70u, 84u, 255u,
            56u, 70u, 84u, 255u,
            72u, 90u, 108u, 255u,
        };
        fallbackSpriteTexture = createTexture(
            kSpriteFallback, 2, 2, false, 33071, 33071, true);

        if (!recreateSwapchain()) {
            throw std::runtime_error("Vulkan swapchain cannot be created for a zero-sized window.");
        }
        configureScreenshotCapture();

        engine::render::parity_contract::RuntimeConfig parityConfig =
            engine::render::parity_contract::makeBaselineConfig();
        parityConfig.framebufferSrgbEnabled = false;
        engine::render::vulkan_backend::applyNeutralPmremParityContract(
            neutralPmremTexture.format,
            parityConfig);
        engine::render::parity_contract::logValidation("Vulkan", parityConfig);
        std::cout << "[Vulkan] Initialized adapter='" << gpuName
                  << "' api=" << VK_VERSION_MAJOR(physicalDeviceProperties.apiVersion)
                  << "." << VK_VERSION_MINOR(physicalDeviceProperties.apiVersion)
                  << "." << VK_VERSION_PATCH(physicalDeviceProperties.apiVersion)
                  << " swapchain=" << swapchainExtent.width << "x" << swapchainExtent.height
                  << " dualSourceBlend=" << (dualSourceBlendSupported ? 1 : 0)
                  << " descriptorIndexing=" << (descriptorIndexingSupported ? 1 : 0)
                  << " indirectWorld=" << (indirectWorldBatchingSupported ? 1 : 0)
                  << " vsync=" << (vsyncEnabled ? 1 : 0) << "\n"
                  << std::flush;
        initialized = true;
    } catch (...) {
        shutdown();
        throw;
    }
}

void VulkanRenderBackendImpl::createInstance() {
    unsigned int extensionCount = 0u;
    if (SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr) != SDL_TRUE) {
        throw std::runtime_error(
            std::string("SDL_Vulkan_GetInstanceExtensions(count) failed: ") + SDL_GetError());
    }
    std::vector<const char*> extensions(extensionCount);
    if (extensionCount > 0u &&
        SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data()) != SDL_TRUE) {
        throw std::runtime_error(
            std::string("SDL_Vulkan_GetInstanceExtensions(names) failed: ") + SDL_GetError());
    }

    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "Pokemon Autochess";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.pEngineName = "PokemonAutochess Engine";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions.data();
    requireVk(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
}

void VulkanRenderBackendImpl::selectPhysicalDevice(const std::string& preferredAdapterName) {
    std::uint32_t deviceCount = 0u;
    requireVk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr),
              "vkEnumeratePhysicalDevices(count)");
    if (deviceCount == 0u) {
        throw std::runtime_error("No Vulkan physical devices were found.");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    requireVk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()),
              "vkEnumeratePhysicalDevices(devices)");

    VkPhysicalDevice best = VK_NULL_HANDLE;
    QueueFamilies bestFamilies;
    int bestScore = std::numeric_limits<int>::min();
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        const QueueFamilies families = findQueueFamilies(candidate, surface);
        if (!families.complete() ||
            !hasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
            !hasSwapchainSupport(candidate, surface)) {
            continue;
        }

        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 10000;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 1000;
        score += static_cast<int>(std::min<std::uint32_t>(
            properties.limits.maxImageDimension2D, 8192u));
        if (!preferredAdapterName.empty() &&
            containsCaseInsensitive(properties.deviceName, preferredAdapterName)) {
            score += 1000000;
        }
        if (score > bestScore) {
            best = candidate;
            bestFamilies = families;
            bestScore = score;
        }
    }
    if (best == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "No Vulkan device supports graphics, presentation, and VK_KHR_swapchain.");
    }

    physicalDevice = best;
    graphicsQueueFamily = bestFamilies.graphics;
    presentQueueFamily = bestFamilies.present;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    gpuName = physicalDeviceProperties.deviceName;
    gpuDiscrete = physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    timestampPeriodNs = physicalDeviceProperties.limits.timestampPeriod;
}

void VulkanRenderBackendImpl::createDevice() {
    const std::set<std::uint32_t> uniqueFamilies{graphicsQueueFamily, presentQueueFamily};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    constexpr float priority = 1.0f;
    for (std::uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1u;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceDescriptorIndexingFeatures availableDescriptorIndexing{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceVulkan11Features availableVulkan11{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    availableVulkan11.pNext = &availableDescriptorIndexing;
    VkPhysicalDeviceFeatures2 availableFeatures2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    availableFeatures2.pNext = &availableVulkan11;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &availableFeatures2);
    const VkPhysicalDeviceFeatures& availableFeatures = availableFeatures2.features;
    VkPhysicalDeviceFeatures enabledFeatures{};
    samplerAnisotropyEnabled = availableFeatures.samplerAnisotropy == VK_TRUE;
    enabledFeatures.samplerAnisotropy = samplerAnisotropyEnabled ? VK_TRUE : VK_FALSE;
    dualSourceBlendSupported = availableFeatures.dualSrcBlend == VK_TRUE;
    enabledFeatures.dualSrcBlend = dualSourceBlendSupported ? VK_TRUE : VK_FALSE;
    constexpr std::uint32_t kRequiredIndexedDescriptors =
        engine::render::vulkan_backend::kMaxIndexedWorldMaterials *
        engine::render::vulkan_backend::kWorldMaterialTextureCount;
    descriptorIndexingSupported =
        !engine::env::flagEnabled("PAC_VULKAN_DISABLE_DESCRIPTOR_INDEXING") &&
        hasDeviceExtension(physicalDevice, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) &&
        availableDescriptorIndexing.shaderSampledImageArrayNonUniformIndexing == VK_TRUE &&
        physicalDeviceProperties.limits.maxPerStageDescriptorSamplers >=
            kRequiredIndexedDescriptors &&
        physicalDeviceProperties.limits.maxDescriptorSetSamplers >=
            kRequiredIndexedDescriptors &&
        physicalDeviceProperties.limits.maxPerStageDescriptorSampledImages >=
            kRequiredIndexedDescriptors &&
        physicalDeviceProperties.limits.maxDescriptorSetSampledImages >=
            kRequiredIndexedDescriptors &&
        physicalDeviceProperties.limits.maxPerStageResources >=
            kRequiredIndexedDescriptors;
    indirectWorldBatchingSupported =
        !engine::env::flagEnabled("PAC_VULKAN_DISABLE_INDIRECT_WORLD") &&
        descriptorIndexingSupported &&
        availableFeatures.multiDrawIndirect == VK_TRUE &&
        availableVulkan11.shaderDrawParameters == VK_TRUE &&
        physicalDeviceProperties.limits.maxDrawIndirectCount > 1u;
    enabledFeatures.multiDrawIndirect =
        indirectWorldBatchingSupported ? VK_TRUE : VK_FALSE;
    maxSamplerAnisotropy = samplerAnisotropyEnabled
        ? std::min(16.0f, physicalDeviceProperties.limits.maxSamplerAnisotropy)
        : 1.0f;

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (descriptorIndexingSupported) {
        extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    }
    VkPhysicalDeviceVulkan11Features enabledVulkan11{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    enabledVulkan11.shaderDrawParameters =
        indirectWorldBatchingSupported ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceDescriptorIndexingFeatures enabledDescriptorIndexing{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    enabledDescriptorIndexing.shaderSampledImageArrayNonUniformIndexing =
        descriptorIndexingSupported ? VK_TRUE : VK_FALSE;
    if (descriptorIndexingSupported) {
        enabledVulkan11.pNext = &enabledDescriptorIndexing;
    }
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &enabledVulkan11;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount =
        static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.pEnabledFeatures = &enabledFeatures;
    requireVk(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, graphicsQueueFamily, 0u, &graphicsQueue);
    vkGetDeviceQueue(device, presentQueueFamily, 0u, &presentQueue);
}

void VulkanRenderBackendImpl::createCommandResources() {
    VkCommandPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                       VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    createInfo.queueFamilyIndex = graphicsQueueFamily;
    requireVk(vkCreateCommandPool(device, &createInfo, nullptr, &commandPool),
              "vkCreateCommandPool");
}

void VulkanRenderBackendImpl::createDescriptorResources() {
    std::array<
        VkDescriptorSetLayoutBinding,
        engine::render::vulkan_backend::kWorldMaterialTextureCount> textureBindings{};
    for (std::uint32_t i = 0u; i < textureBindings.size(); ++i) {
        textureBindings[i].binding = i;
        textureBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureBindings[i].descriptorCount = 1u;
        textureBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = static_cast<std::uint32_t>(textureBindings.size());
    setLayoutInfo.pBindings = textureBindings.data();
    requireVk(vkCreateDescriptorSetLayout(
                  device, &setLayoutInfo, nullptr, &textureSetLayout),
              "vkCreateDescriptorSetLayout");

    if (descriptorIndexingSupported) {
        for (VkDescriptorSetLayoutBinding& binding : textureBindings) {
            binding.descriptorCount =
                engine::render::vulkan_backend::kMaxIndexedWorldMaterials;
        }
        setLayoutInfo.bindingCount =
            static_cast<std::uint32_t>(textureBindings.size());
        setLayoutInfo.pBindings = textureBindings.data();
        requireVk(vkCreateDescriptorSetLayout(
                      device,
                      &setLayoutInfo,
                      nullptr,
                      &indexedWorldMaterialSetLayout),
                  "vkCreateDescriptorSetLayout(indexed world materials)");
    }

    std::array<VkDescriptorSetLayoutBinding, 6> worldStateBindings{};
    worldStateBindings[0].binding = 0u;
    worldStateBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    worldStateBindings[0].descriptorCount = 1u;
    worldStateBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    worldStateBindings[1].binding = 1u;
    worldStateBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    worldStateBindings[1].descriptorCount = 1u;
    worldStateBindings[1].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    worldStateBindings[2].binding = 2u;
    worldStateBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    worldStateBindings[2].descriptorCount = 1u;
    worldStateBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    worldStateBindings[3].binding = 3u;
    worldStateBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    worldStateBindings[3].descriptorCount = 1u;
    worldStateBindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    worldStateBindings[4].binding = 4u;
    worldStateBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    worldStateBindings[4].descriptorCount = 1u;
    worldStateBindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    worldStateBindings[5].binding = 5u;
    worldStateBindings[5].descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    worldStateBindings[5].descriptorCount = 1u;
    worldStateBindings[5].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo worldStateLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    worldStateLayoutInfo.bindingCount =
        static_cast<std::uint32_t>(worldStateBindings.size());
    worldStateLayoutInfo.pBindings = worldStateBindings.data();
    requireVk(vkCreateDescriptorSetLayout(
                  device, &worldStateLayoutInfo, nullptr, &worldStateSetLayout),
              "vkCreateDescriptorSetLayout(world state)");

    VkDescriptorSetLayoutBinding sceneColorBinding{};
    sceneColorBinding.binding = 0u;
    sceneColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sceneColorBinding.descriptorCount = 1u;
    sceneColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sceneColorSetLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sceneColorSetLayoutInfo.bindingCount = 1u;
    sceneColorSetLayoutInfo.pBindings = &sceneColorBinding;
    requireVk(vkCreateDescriptorSetLayout(
                  device,
                  &sceneColorSetLayoutInfo,
                  nullptr,
                  &worldSceneColorSetLayout),
              "vkCreateDescriptorSetLayout(world scene color)");

    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount =
        4096u * engine::render::vulkan_backend::kWorldMaterialTextureCount +
        kFramesInFlight +
        (descriptorIndexingSupported
             ? kFramesInFlight *
                   engine::render::vulkan_backend::kMaxIndexedWorldMaterials *
                   engine::render::vulkan_backend::kWorldMaterialTextureCount
             : 0u);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[1].descriptorCount = kFramesInFlight * 3u;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kFramesInFlight * 2u;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    poolSizes[3].descriptorCount = kFramesInFlight;
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 4096u + kFramesInFlight + kFramesInFlight +
                       (descriptorIndexingSupported ? kFramesInFlight : 0u);
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    requireVk(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
              "vkCreateDescriptorPool");

    VkPushConstantRange debugPushRange{};
    debugPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    debugPushRange.size = static_cast<std::uint32_t>(sizeof(DebugPushConstants));
    VkPipelineLayoutCreateInfo debugLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    debugLayoutInfo.pushConstantRangeCount = 1u;
    debugLayoutInfo.pPushConstantRanges = &debugPushRange;
    requireVk(vkCreatePipelineLayout(device, &debugLayoutInfo, nullptr, &debugPipelineLayout),
              "vkCreatePipelineLayout(debug)");

    VkPushConstantRange texturedPushRange{};
    texturedPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    texturedPushRange.size = static_cast<std::uint32_t>(sizeof(WorldPushConstants));
    VkPipelineLayoutCreateInfo texturedLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    const std::array<VkDescriptorSetLayout, 2> texturedSetLayouts{
        textureSetLayout,
        worldStateSetLayout,
    };
    texturedLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(texturedSetLayouts.size());
    texturedLayoutInfo.pSetLayouts = texturedSetLayouts.data();
    texturedLayoutInfo.pushConstantRangeCount = 1u;
    texturedLayoutInfo.pPushConstantRanges = &texturedPushRange;
    requireVk(vkCreatePipelineLayout(
                  device, &texturedLayoutInfo, nullptr, &texturedPipelineLayout),
              "vkCreatePipelineLayout(textured)");

    VkPipelineLayoutCreateInfo sceneColorLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    sceneColorLayoutInfo.setLayoutCount = 1u;
    sceneColorLayoutInfo.pSetLayouts = &worldSceneColorSetLayout;
    requireVk(vkCreatePipelineLayout(
                  device,
                  &sceneColorLayoutInfo,
                  nullptr,
                  &worldSceneColorPipelineLayout),
              "vkCreatePipelineLayout(world scene color)");

    std::array<VkDescriptorSetLayout, kFramesInFlight> sceneColorSetLayouts{};
    sceneColorSetLayouts.fill(worldSceneColorSetLayout);
    VkDescriptorSetAllocateInfo sceneColorAllocateInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sceneColorAllocateInfo.descriptorPool = descriptorPool;
    sceneColorAllocateInfo.descriptorSetCount = kFramesInFlight;
    sceneColorAllocateInfo.pSetLayouts = sceneColorSetLayouts.data();
    requireVk(vkAllocateDescriptorSets(
                  device,
                  &sceneColorAllocateInfo,
                  worldSceneColorDescriptorSets.data()),
              "vkAllocateDescriptorSets(world scene color)");

    VkSamplerCreateInfo sceneColorSamplerInfo{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sceneColorSamplerInfo.magFilter = VK_FILTER_NEAREST;
    sceneColorSamplerInfo.minFilter = VK_FILTER_NEAREST;
    sceneColorSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sceneColorSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sceneColorSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sceneColorSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sceneColorSamplerInfo.maxLod = 0.0f;
    requireVk(vkCreateSampler(
                  device,
                  &sceneColorSamplerInfo,
                  nullptr,
                  &worldSceneColorSampler),
              "vkCreateSampler(world scene color)");

    if (descriptorIndexingSupported) {
        const std::array<VkDescriptorSetLayout, 2> indirectSetLayouts{
            indexedWorldMaterialSetLayout,
            worldStateSetLayout,
        };
        VkPushConstantRange indirectPushRange{};
        indirectPushRange.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        indirectPushRange.size = static_cast<std::uint32_t>(
            sizeof(engine::render::vulkan_backend::WorldIndirectPushConstants));
        VkPipelineLayoutCreateInfo indirectLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        indirectLayoutInfo.setLayoutCount =
            static_cast<std::uint32_t>(indirectSetLayouts.size());
        indirectLayoutInfo.pSetLayouts = indirectSetLayouts.data();
        indirectLayoutInfo.pushConstantRangeCount = 1u;
        indirectLayoutInfo.pPushConstantRanges = &indirectPushRange;
        requireVk(vkCreatePipelineLayout(
                      device,
                      &indirectLayoutInfo,
                      nullptr,
                      &indirectWorldPipelineLayout),
                  "vkCreatePipelineLayout(indirect world)");
    }
}

void VulkanRenderBackendImpl::createFrameResources() {
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = kFramesInFlight;
    requireVk(vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers.data()),
              "vkAllocateCommandBuffers");

    for (std::uint32_t i = 0u; i < kFramesInFlight; ++i) {
        FrameResources& frame = frames[i];
        frame.commandBuffer = commandBuffers[i];
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        requireVk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable),
                  "vkCreateSemaphore(imageAvailable)");
        requireVk(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.renderFinished),
                  "vkCreateSemaphore(renderFinished)");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        requireVk(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight),
                  "vkCreateFence");

        if (physicalDeviceProperties.limits.timestampComputeAndGraphics == VK_TRUE &&
            timestampPeriodNs > 0.0f) {
            VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = 2u;
            requireVk(vkCreateQueryPool(device, &queryInfo, nullptr, &frame.timestampQueries),
                      "vkCreateQueryPool");
        }
        frame.transient = createBuffer(
            kTransientBytesPerFrame,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDescriptorSetAllocateInfo descriptorAllocate{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        descriptorAllocate.descriptorPool = descriptorPool;
        descriptorAllocate.descriptorSetCount = 1u;
        descriptorAllocate.pSetLayouts = &worldStateSetLayout;
        requireVk(vkAllocateDescriptorSets(
                      device,
                      &descriptorAllocate,
                      &frame.worldStateDescriptorSet),
                  "vkAllocateDescriptorSets(world state)");
        if (descriptorIndexingSupported) {
            descriptorAllocate.pSetLayouts = &indexedWorldMaterialSetLayout;
            requireVk(vkAllocateDescriptorSets(
                          device,
                          &descriptorAllocate,
                          &frame.indexedWorldMaterialDescriptorSet),
                      "vkAllocateDescriptorSets(indexed world materials)");
        }
        const std::array<VkDeviceSize, 6> ranges{
            sizeof(engine::render::vulkan_backend::WorldViewState),
            sizeof(engine::render::vulkan_backend::WorldSpecializedMaterialState),
            sizeof(engine::render::vulkan_backend::WorldTransformState),
            frame.transient.size,
            frame.transient.size,
            VK_WHOLE_SIZE,
        };
        std::array<VkDescriptorBufferInfo, 6> bufferInfos{};
        std::array<VkWriteDescriptorSet, 6> writes{};
        for (std::uint32_t binding = 0u; binding < writes.size(); ++binding) {
            bufferInfos[binding].buffer = frame.transient.buffer;
            bufferInfos[binding].offset = 0u;
            bufferInfos[binding].range = ranges[binding];
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = frame.worldStateDescriptorSet;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1u;
            writes[binding].descriptorType = binding < 3u
                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                : binding == 5u
                    ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                    : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[binding].pBufferInfo = &bufferInfos[binding];
        }
        vkUpdateDescriptorSets(
            device,
            static_cast<std::uint32_t>(writes.size()),
            writes.data(),
            0u,
            nullptr);
    }
}

bool VulkanRenderBackendImpl::recreateSwapchain() {
    if (device == VK_NULL_HANDLE || window == nullptr) return false;
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        return false;
    }
    requestedWidth = drawableWidth;
    requestedHeight = drawableHeight;
    requireVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(recreateSwapchain)");
    destroySwapchainResources();
    createSwapchainResources();
    swapchainDirty = false;
    return true;
}

void VulkanRenderBackendImpl::createSwapchainResources() {
    VkSurfaceCapabilitiesKHR capabilities{};
    requireVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                  physicalDevice, surface, &capabilities),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    std::uint32_t formatCount = 0u;
    requireVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                  physicalDevice, surface, &formatCount, nullptr),
              "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    requireVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                  physicalDevice, surface, &formatCount, formats.data()),
              "vkGetPhysicalDeviceSurfaceFormatsKHR(formats)");

    std::uint32_t modeCount = 0u;
    requireVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                  physicalDevice, surface, &modeCount, nullptr),
              "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    std::vector<VkPresentModeKHR> modes(modeCount);
    requireVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                  physicalDevice, surface, &modeCount, modes.data()),
              "vkGetPhysicalDeviceSurfacePresentModesKHR(modes)");

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    swapchainFormat = surfaceFormat.format;
    swapchainColorSpace = surfaceFormat.colorSpace;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        swapchainExtent = capabilities.currentExtent;
    } else {
        swapchainExtent.width = std::clamp(
            static_cast<std::uint32_t>(requestedWidth),
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        swapchainExtent.height = std::clamp(
            static_cast<std::uint32_t>(requestedHeight),
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1u;
    if (capabilities.maxImageCount > 0u) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    const std::uint32_t queueFamilyIndices[]{graphicsQueueFamily, presentQueueFamily};
    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = swapchainFormat;
    createInfo.imageColorSpace = swapchainColorSpace;
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1u;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainTransferSourceSupported =
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    if (swapchainTransferSourceSupported) {
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (graphicsQueueFamily != presentQueueFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2u;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
    createInfo.presentMode = choosePresentMode(modes, vsyncEnabled);
    createInfo.clipped = VK_TRUE;
    requireVk(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain),
              "vkCreateSwapchainKHR");

    requireVk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr),
              "vkGetSwapchainImagesKHR(count)");
    swapchainImages.resize(imageCount);
    requireVk(vkGetSwapchainImagesKHR(
                  device, swapchain, &imageCount, swapchainImages.data()),
              "vkGetSwapchainImagesKHR(images)");
    swapchainImageViews.resize(imageCount, VK_NULL_HANDLE);
    for (std::uint32_t i = 0u; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1u;
        viewInfo.subresourceRange.layerCount = 1u;
        requireVk(vkCreateImageView(
                      device, &viewInfo, nullptr, &swapchainImageViews[i]),
                  "vkCreateImageView(swapchain)");
    }
    imagesInFlight.assign(imageCount, VK_NULL_HANDLE);

    createRenderPass();
    createDepthResources();
    createWorldSceneColorResources();
    createFramebuffers();
    createPipelines();
}

void VulkanRenderBackendImpl::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    depthFormat = VK_FORMAT_D32_SFLOAT;
    const std::array<VkFormat, 3> depthCandidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };
    for (VkFormat candidate : depthCandidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u) {
            depthFormat = candidate;
            break;
        }
    }

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    const auto createCompatibleRenderPass =
        [&](const VkAttachmentDescription& color,
            const VkAttachmentDescription& depth,
            const VkSubpassDependency* dependencies,
            std::uint32_t dependencyCount,
            VkRenderPass& outRenderPass,
            const char* operation) {
            const std::array<VkAttachmentDescription, 2> attachments{color, depth};
            VkRenderPassCreateInfo createInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            createInfo.attachmentCount =
                static_cast<std::uint32_t>(attachments.size());
            createInfo.pAttachments = attachments.data();
            createInfo.subpassCount = 1u;
            createInfo.pSubpasses = &subpass;
            createInfo.dependencyCount = dependencyCount;
            createInfo.pDependencies = dependencies;
            requireVk(
                vkCreateRenderPass(
                    device, &createInfo, nullptr, &outRenderPass),
                operation);
        };

    VkSubpassDependency beginDependency{};
    beginDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    beginDependency.dstSubpass = 0u;
    beginDependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    beginDependency.dstStageMask = beginDependency.srcStageMask;
    beginDependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    createCompatibleRenderPass(
        colorAttachment,
        depthAttachment,
        &beginDependency,
        1u,
        renderPass,
        "vkCreateRenderPass");

    VkAttachmentDescription loadColorAttachment = colorAttachment;
    loadColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    loadColorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription loadDepthAttachment = depthAttachment;
    loadDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    loadDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    loadDepthAttachment.initialLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkSubpassDependency loadDependency{};
    loadDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    loadDependency.dstSubpass = 0u;
    loadDependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    loadDependency.dstStageMask = loadDependency.srcStageMask;
    loadDependency.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    loadDependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    createCompatibleRenderPass(
        loadColorAttachment,
        loadDepthAttachment,
        &loadDependency,
        1u,
        loadRenderPass,
        "vkCreateRenderPass(load)");

    VkAttachmentDescription sceneColorAttachment = colorAttachment;
    sceneColorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentDescription sceneDepthAttachment = depthAttachment;
    sceneDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const std::array<VkSubpassDependency, 2> sceneDependencies{
        beginDependency,
        VkSubpassDependency{
            0u,
            VK_SUBPASS_EXTERNAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            0u},
    };
    createCompatibleRenderPass(
        sceneColorAttachment,
        sceneDepthAttachment,
        sceneDependencies.data(),
        static_cast<std::uint32_t>(sceneDependencies.size()),
        worldSceneColorRenderPass,
        "vkCreateRenderPass(world scene color)");
}

void VulkanRenderBackendImpl::createDepthResources() {
    const std::size_t count = swapchainImages.size();
    depthImages.resize(count, VK_NULL_HANDLE);
    depthMemories.resize(count, VK_NULL_HANDLE);
    depthViews.resize(count, VK_NULL_HANDLE);
    for (std::size_t i = 0u; i < count; ++i) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = depthFormat;
        imageInfo.extent = {swapchainExtent.width, swapchainExtent.height, 1u};
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        requireVk(vkCreateImage(device, &imageInfo, nullptr, &depthImages[i]),
                  "vkCreateImage(depth)");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, depthImages[i], &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        requireVk(vkAllocateMemory(device, &allocation, nullptr, &depthMemories[i]),
                  "vkAllocateMemory(depth)");
        requireVk(vkBindImageMemory(device, depthImages[i], depthMemories[i], 0u),
                  "vkBindImageMemory(depth)");

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = depthImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
            depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        viewInfo.subresourceRange.levelCount = 1u;
        viewInfo.subresourceRange.layerCount = 1u;
        requireVk(vkCreateImageView(device, &viewInfo, nullptr, &depthViews[i]),
                  "vkCreateImageView(depth)");
    }
}

void VulkanRenderBackendImpl::createFramebuffers() {
    framebuffers.resize(swapchainImageViews.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0u; i < framebuffers.size(); ++i) {
        const VkImageView attachments[]{swapchainImageViews[i], depthViews[i]};
        VkFramebufferCreateInfo createInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        createInfo.renderPass = renderPass;
        createInfo.attachmentCount = 2u;
        createInfo.pAttachments = attachments;
        createInfo.width = swapchainExtent.width;
        createInfo.height = swapchainExtent.height;
        createInfo.layers = 1u;
        requireVk(vkCreateFramebuffer(device, &createInfo, nullptr, &framebuffers[i]),
                  "vkCreateFramebuffer");
    }
}

VkShaderModule VulkanRenderBackendImpl::loadShaderModule(const char* fileName) const {
    const std::filesystem::path path = std::filesystem::path(PAC_VULKAN_SHADER_DIR) / fileName;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open Vulkan shader: " + path.string());
    }
    const std::streamsize byteCount = input.tellg();
    if (byteCount <= 0 || (byteCount % 4) != 0) {
        throw std::runtime_error("Invalid SPIR-V byte count for: " + path.string());
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    if (!input.read(reinterpret_cast<char*>(words.data()), byteCount)) {
        throw std::runtime_error("Unable to read Vulkan shader: " + path.string());
    }
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = static_cast<std::size_t>(byteCount);
    createInfo.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    requireVk(vkCreateShaderModule(device, &createInfo, nullptr, &module),
              "vkCreateShaderModule");
    return module;
}

void VulkanRenderBackendImpl::createPipelines() {
    VkShaderModule debugVs = VK_NULL_HANDLE;
    VkShaderModule debugFs = VK_NULL_HANDLE;
    VkShaderModule spriteVs = VK_NULL_HANDLE;
    VkShaderModule spriteFs = VK_NULL_HANDLE;
    VkShaderModule sceneColorVs = VK_NULL_HANDLE;
    VkShaderModule sceneColorFs = VK_NULL_HANDLE;
    VkShaderModule worldVs = VK_NULL_HANDLE;
    VkShaderModule worldFs = VK_NULL_HANDLE;
    VkShaderModule worldDualSourceFs = VK_NULL_HANDLE;
    VkShaderModule worldIndirectVs = VK_NULL_HANDLE;
    VkShaderModule worldIndirectFs = VK_NULL_HANDLE;
    VkShaderModule worldIndirectDualSourceFs = VK_NULL_HANDLE;
    try {
        debugVs = loadShaderModule("debug.vert.spv");
        debugFs = loadShaderModule("debug.frag.spv");
        spriteVs = loadShaderModule("sprite.vert.spv");
        spriteFs = loadShaderModule("sprite.frag.spv");
        sceneColorVs = loadShaderModule("scene_color.vert.spv");
        sceneColorFs = loadShaderModule("scene_color.frag.spv");
        worldVs = loadShaderModule("world.vert.spv");
        worldFs = loadShaderModule("world.frag.spv");
        if (dualSourceBlendSupported) {
            worldDualSourceFs = loadShaderModule("world_dual_source.frag.spv");
        }
        if (indirectWorldBatchingSupported) {
            worldIndirectVs = loadShaderModule("world_indirect.vert.spv");
            worldIndirectFs = loadShaderModule("world_indirect.frag.spv");
            if (dualSourceBlendSupported) {
                worldIndirectDualSourceFs = loadShaderModule(
                    "world_indirect_dual_source.frag.spv");
            }
        }

        VkVertexInputBindingDescription debugBinding{0u, sizeof(DebugVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::vector<VkVertexInputAttributeDescription> debugAttributes{
            {0u, 0u, VK_FORMAT_R32G32_SFLOAT, offsetof(DebugVertex, x)},
            {1u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DebugVertex, r)},
        };
        debugPipeline = createGraphicsPipeline(device,
                                               renderPass,
                                               debugPipelineLayout,
                                               debugVs,
                                               debugFs,
                                               debugBinding,
                                               debugAttributes,
                                               true,
                                               0u,
                                               false,
                                               false,
                                               false);

        VkVertexInputBindingDescription spriteBinding{
            0u,
            sizeof(engine::render::vulkan_backend::SpriteInstanceState),
            VK_VERTEX_INPUT_RATE_INSTANCE};
        const std::vector<VkVertexInputAttributeDescription> spriteAttributes{
            {0u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(engine::render::vulkan_backend::SpriteInstanceState, rectPx)},
            {1u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(engine::render::vulkan_backend::SpriteInstanceState, uvRect)},
            {2u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(engine::render::vulkan_backend::SpriteInstanceState, color)},
        };
        spritePipeline = createGraphicsPipeline(device,
                                                renderPass,
                                                texturedPipelineLayout,
                                                spriteVs,
                                                spriteFs,
                                                spriteBinding,
                                                spriteAttributes,
                                                true,
                                                0u,
                                                false,
                                                false,
                                                false);

        VkVertexInputBindingDescription sceneColorBinding{
            0u, 0u, VK_VERTEX_INPUT_RATE_VERTEX};
        const std::vector<VkVertexInputAttributeDescription>
            sceneColorAttributes{};
        worldSceneColorPipeline = createGraphicsPipeline(
            device,
            loadRenderPass,
            worldSceneColorPipelineLayout,
            sceneColorVs,
            sceneColorFs,
            sceneColorBinding,
            sceneColorAttributes,
            false,
            0u,
            false,
            false,
            false);

        VkVertexInputBindingDescription worldBinding{
            0u,
            sizeof(IRenderBackend::WorldMeshVertex),
            VK_VERTEX_INPUT_RATE_VERTEX};
        const std::vector<VkVertexInputAttributeDescription> worldAttributes{
            {0u, 0u, VK_FORMAT_R32G32B32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, x)},
            {1u, 0u, VK_FORMAT_R32G32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, u)},
            {2u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, r)},
            {3u, 0u, VK_FORMAT_R32G32B32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, nx)},
            {4u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, joint0)},
            {5u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, weight0)},
            {6u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, tx)},
            {7u, 0u, VK_FORMAT_R32G32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, sourceUv1U)},
            {8u, 0u, VK_FORMAT_R32G32_SFLOAT,
             offsetof(IRenderBackend::WorldMeshVertex, sourceUv2U)},
        };
        worldPipelines[0] = createGraphicsPipeline(device, renderPass, texturedPipelineLayout,
                                                   worldVs, worldFs, worldBinding, worldAttributes,
                                                   false, 0u, false, true, true);
        worldPipelines[1] = createGraphicsPipeline(device, renderPass, texturedPipelineLayout,
                                                   worldVs, worldFs, worldBinding, worldAttributes,
                                                   false, 0u, false, false, false);
        for (std::uint8_t blendMode = 0u; blendMode < 3u; ++blendMode) {
            const std::size_t base = 2u + static_cast<std::size_t>(blendMode) * 2u;
            worldPipelines[base] = createGraphicsPipeline(device, renderPass, texturedPipelineLayout,
                                                          worldVs, worldFs, worldBinding, worldAttributes,
                                                          true, blendMode, false, true, false);
            worldPipelines[base + 1u] = createGraphicsPipeline(
                device, renderPass, texturedPipelineLayout,
                worldVs, worldFs, worldBinding, worldAttributes,
                true, blendMode, false, false, false);
        }
        if (dualSourceBlendSupported) {
            for (std::uint8_t blendMode = 0u; blendMode < 2u; ++blendMode) {
                const std::size_t base =
                    engine::render::vulkan_backend::kFirstDualSourceBlendPipeline +
                    static_cast<std::size_t>(blendMode) * 2u;
                worldPipelines[base] = createGraphicsPipeline(
                    device, renderPass, texturedPipelineLayout,
                    worldVs, worldDualSourceFs, worldBinding, worldAttributes,
                    true, blendMode, true, true, false);
                worldPipelines[base + 1u] = createGraphicsPipeline(
                    device, renderPass, texturedPipelineLayout,
                    worldVs, worldDualSourceFs, worldBinding, worldAttributes,
                    true, blendMode, true, false, false);
            }
        }
        if (indirectWorldBatchingSupported) {
            indirectWorldPipelines[0] = createGraphicsPipeline(
                device, renderPass, indirectWorldPipelineLayout,
                worldIndirectVs, worldIndirectFs, worldBinding, worldAttributes,
                false, 0u, false, true, true);
            indirectWorldPipelines[1] = createGraphicsPipeline(
                device, renderPass, indirectWorldPipelineLayout,
                worldIndirectVs, worldIndirectFs, worldBinding, worldAttributes,
                false, 0u, false, false, false);
            for (std::uint8_t blendMode = 0u; blendMode < 3u; ++blendMode) {
                const std::size_t base =
                    2u + static_cast<std::size_t>(blendMode) * 2u;
                indirectWorldPipelines[base] = createGraphicsPipeline(
                    device, renderPass, indirectWorldPipelineLayout,
                    worldIndirectVs, worldIndirectFs, worldBinding, worldAttributes,
                    true, blendMode, false, true, false);
                indirectWorldPipelines[base + 1u] = createGraphicsPipeline(
                    device, renderPass, indirectWorldPipelineLayout,
                    worldIndirectVs, worldIndirectFs, worldBinding, worldAttributes,
                    true, blendMode, false, false, false);
            }
            if (dualSourceBlendSupported) {
                for (std::uint8_t blendMode = 0u; blendMode < 2u; ++blendMode) {
                    const std::size_t base =
                        engine::render::vulkan_backend::kFirstDualSourceBlendPipeline +
                        static_cast<std::size_t>(blendMode) * 2u;
                    indirectWorldPipelines[base] = createGraphicsPipeline(
                        device, renderPass, indirectWorldPipelineLayout,
                        worldIndirectVs, worldIndirectDualSourceFs,
                        worldBinding, worldAttributes,
                        true, blendMode, true, true, false);
                    indirectWorldPipelines[base + 1u] = createGraphicsPipeline(
                        device, renderPass, indirectWorldPipelineLayout,
                        worldIndirectVs, worldIndirectDualSourceFs,
                        worldBinding, worldAttributes,
                        true, blendMode, true, false, false);
                }
            }
        }
    } catch (...) {
        if (debugVs != VK_NULL_HANDLE) vkDestroyShaderModule(device, debugVs, nullptr);
        if (debugFs != VK_NULL_HANDLE) vkDestroyShaderModule(device, debugFs, nullptr);
        if (spriteVs != VK_NULL_HANDLE) vkDestroyShaderModule(device, spriteVs, nullptr);
        if (spriteFs != VK_NULL_HANDLE) vkDestroyShaderModule(device, spriteFs, nullptr);
        if (sceneColorVs != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, sceneColorVs, nullptr);
        }
        if (sceneColorFs != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, sceneColorFs, nullptr);
        }
        if (worldVs != VK_NULL_HANDLE) vkDestroyShaderModule(device, worldVs, nullptr);
        if (worldFs != VK_NULL_HANDLE) vkDestroyShaderModule(device, worldFs, nullptr);
        if (worldDualSourceFs != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, worldDualSourceFs, nullptr);
        }
        if (worldIndirectVs != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, worldIndirectVs, nullptr);
        }
        if (worldIndirectFs != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, worldIndirectFs, nullptr);
        }
        if (worldIndirectDualSourceFs != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, worldIndirectDualSourceFs, nullptr);
        }
        throw;
    }
    vkDestroyShaderModule(device, debugVs, nullptr);
    vkDestroyShaderModule(device, debugFs, nullptr);
    vkDestroyShaderModule(device, spriteVs, nullptr);
    vkDestroyShaderModule(device, spriteFs, nullptr);
    vkDestroyShaderModule(device, sceneColorVs, nullptr);
    vkDestroyShaderModule(device, sceneColorFs, nullptr);
    vkDestroyShaderModule(device, worldVs, nullptr);
    vkDestroyShaderModule(device, worldFs, nullptr);
    if (worldDualSourceFs != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, worldDualSourceFs, nullptr);
    }
    if (worldIndirectVs != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, worldIndirectVs, nullptr);
    }
    if (worldIndirectFs != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, worldIndirectFs, nullptr);
    }
    if (worldIndirectDualSourceFs != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, worldIndirectDualSourceFs, nullptr);
    }
}

void VulkanRenderBackendImpl::destroyPipelines() {
    if (device == VK_NULL_HANDLE) return;
    if (debugPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, debugPipeline, nullptr);
    if (spritePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, spritePipeline, nullptr);
    if (worldSceneColorPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, worldSceneColorPipeline, nullptr);
    }
    debugPipeline = VK_NULL_HANDLE;
    spritePipeline = VK_NULL_HANDLE;
    worldSceneColorPipeline = VK_NULL_HANDLE;
    for (VkPipeline& pipeline : worldPipelines) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    for (VkPipeline& pipeline : indirectWorldPipelines) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}

void VulkanRenderBackendImpl::destroySwapchainResources() {
    if (device == VK_NULL_HANDLE) return;
    mainRenderPassActive = false;
    worldSceneColorPassActive = false;
    destroyPipelines();
    destroyWorldSceneColorResources();
    for (VkFramebuffer framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();
    for (VkImageView view : depthViews) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
    }
    for (VkImage image : depthImages) {
        if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
    }
    for (VkDeviceMemory memory : depthMemories) {
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    }
    depthViews.clear();
    depthImages.clear();
    depthMemories.clear();
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
    if (loadRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, loadRenderPass, nullptr);
    }
    if (worldSceneColorRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, worldSceneColorRenderPass, nullptr);
    }
    renderPass = VK_NULL_HANDLE;
    loadRenderPass = VK_NULL_HANDLE;
    worldSceneColorRenderPass = VK_NULL_HANDLE;
    for (VkImageView view : swapchainImageViews) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
    }
    swapchainImageViews.clear();
    swapchainImages.clear();
    imagesInFlight.clear();
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void VulkanRenderBackendImpl::beginFrame(float r, float g, float b, float a) {
    frameActive = false;
    mainRenderPassActive = false;
    worldSceneColorPassActive = false;
    if (!initialized || device == VK_NULL_HANDLE) return;
    ++frameCounter;
    if ((swapchainDirty || swapchain == VK_NULL_HANDLE) && !recreateSwapchain()) return;

    FrameResources& frame = frames[currentFrame];
    requireVk(vkWaitForFences(device, 1u, &frame.inFlight, VK_TRUE, UINT64_MAX),
              "vkWaitForFences(frame)");

    lastTimings.gpuFrameValid = false;
    if (frame.timestampQueries != VK_NULL_HANDLE && frame.timestampIssued) {
        std::array<std::uint64_t, 2> timestamps{};
        const VkResult queryResult = vkGetQueryPoolResults(
            device,
            frame.timestampQueries,
            0u,
            2u,
            sizeof(timestamps),
            timestamps.data(),
            sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (queryResult == VK_SUCCESS && timestamps[1] >= timestamps[0]) {
            const double nanoseconds = static_cast<double>(timestamps[1] - timestamps[0]) *
                                       static_cast<double>(timestampPeriodNs);
            lastTimings.gpuFrameMs = static_cast<float>(nanoseconds / 1000000.0);
            lastTimings.gpuFrameValid = true;
        }
    }

    VkResult acquireResult = vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        frame.imageAvailable,
        VK_NULL_HANDLE,
        &acquiredImage);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainDirty = true;
        if (!recreateSwapchain()) return;
        acquireResult = vkAcquireNextImageKHR(
            device,
            swapchain,
            UINT64_MAX,
            frame.imageAvailable,
            VK_NULL_HANDLE,
            &acquiredImage);
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throwVk("vkAcquireNextImageKHR", acquireResult);
    }
    if (acquireResult == VK_SUBOPTIMAL_KHR) swapchainDirty = true;

    if (imagesInFlight[acquiredImage] != VK_NULL_HANDLE) {
        requireVk(vkWaitForFences(
                      device, 1u, &imagesInFlight[acquiredImage], VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(swapchain image)");
    }
    imagesInFlight[acquiredImage] = frame.inFlight;
    requireVk(vkResetFences(device, 1u, &frame.inFlight), "vkResetFences");
    requireVk(vkResetCommandBuffer(frame.commandBuffer, 0u), "vkResetCommandBuffer");
    frame.transient.offset = 0u;
    frame.indexedWorldMaterialSetBound = false;
    frameStats = {};
    frameSkinPalettes.clear();
    frameSkinPaletteUploadBytes = 0u;
    frameSkinPaletteReuseBytes = 0u;
    frameSkinPaletteUploads = 0u;
    frameSkinPaletteReuses = 0u;
    resetWorldFrameStateCache();
    transientOverflowLogged = false;

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    requireVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    frame.timestampIssued = frame.timestampQueries != VK_NULL_HANDLE;
    if (frame.timestampIssued) {
        vkCmdResetQueryPool(frame.commandBuffer, frame.timestampQueries, 0u, 2u);
        vkCmdWriteTimestamp(frame.commandBuffer,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            frame.timestampQueries,
                            0u);
    }

    const std::array<VkClearValue, 2> clearValues{
        VkClearValue{{{r, g, b, a}}},
        VkClearValue{{{1.0f, 0u}}},
    };
    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[acquiredImage];
    renderPassInfo.renderArea.extent = swapchainExtent;
    renderPassInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(frame.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    frameClearColor = {r, g, b, a};
    mainRenderPassActive = true;

    setViewportAndScissor(
        frame.commandBuffer,
        static_cast<int>(swapchainExtent.width),
        static_cast<int>(swapchainExtent.height));
    frameActive = true;
}

void VulkanRenderBackendImpl::endFrame() {
    if (!frameActive || device == VK_NULL_HANDLE) return;
    if (worldSceneColorPassActive) {
        endWorldSceneColorPass();
    }
    FrameResources& frame = frames[currentFrame];
    if (mainRenderPassActive) {
        vkCmdEndRenderPass(frame.commandBuffer);
        mainRenderPassActive = false;
    }
    const bool capturedThisFrame = recordScreenshotCopy(frame.commandBuffer);
    if (frame.timestampIssued) {
        vkCmdWriteTimestamp(frame.commandBuffer,
                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            frame.timestampQueries,
                            1u);
    }
    requireVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");

    constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1u;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1u;
    submitInfo.pSignalSemaphores = &frame.renderFinished;
    requireVk(vkQueueSubmit(graphicsQueue, 1u, &submitInfo, frame.inFlight), "vkQueueSubmit");

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores = &frame.renderFinished;
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &acquiredImage;
    const auto presentStart = std::chrono::steady_clock::now();
    const VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    const auto presentEnd = std::chrono::steady_clock::now();
    lastTimings.presentWaitMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(presentEnd - presentStart).count());
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        swapchainDirty = true;
    } else if (presentResult != VK_SUCCESS) {
        throwVk("vkQueuePresentKHR", presentResult);
    }
    if (capturedThisFrame) {
        finishScreenshotCapture(frame.inFlight);
    }

    maybeLogWorldFrameCache();
    lastStats = frameStats;
    frameActive = false;
    currentFrame = (currentFrame + 1u) % kFramesInFlight;
}

void VulkanRenderBackendImpl::requestResize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (requestedWidth != width || requestedHeight != height) {
        requestedWidth = width;
        requestedHeight = height;
        swapchainDirty = true;
    }
}

void VulkanRenderBackendImpl::requestVSync(bool enabled) {
    if (vsyncEnabled != enabled) {
        vsyncEnabled = enabled;
        swapchainDirty = true;
    }
}

void VulkanRenderBackendImpl::recordSubmissionStats(
    const IRenderBackend::WorldIndexedSubmissionStats& stats) {
    frameStats.indexedOpaqueDraws += stats.opaqueDraws;
    frameStats.indexedBlendDraws += stats.blendDraws;
    frameStats.indexedCachedDraws += stats.cachedDraws;
    frameStats.indexedDynamicDraws += stats.dynamicDraws;
    frameStats.indexedInstancedDraws += stats.instancedDraws;
    frameStats.indexedOutlineBatches += stats.outlineBatches;
    frameStats.indexedGeometrySwitches += stats.geometrySwitches;
    frameStats.indexedMaterialSwitches += stats.materialSwitches;
    frameStats.indexedTextureSwitches += stats.textureSwitches;
}

std::uint32_t VulkanRenderBackendImpl::findMemoryType(
    std::uint32_t typeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred) const {
    std::uint32_t requiredMatch = UINT32_MAX;
    for (std::uint32_t i = 0u; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) == 0u) continue;
        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
        if ((flags & required) != required) continue;
        if ((flags & preferred) == preferred) return i;
        if (requiredMatch == UINT32_MAX) requiredMatch = i;
    }
    if (requiredMatch != UINT32_MAX) return requiredMatch;
    throw std::runtime_error("No compatible Vulkan memory type was found.");
}

VulkanRenderBackendImpl::Buffer VulkanRenderBackendImpl::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred) {
    Buffer out;
    out.size = size;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    requireVk(vkCreateBuffer(device, &bufferInfo, nullptr, &out.buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, out.buffer, &requirements);
    const std::uint32_t memoryType = findMemoryType(
        requirements.memoryTypeBits, required, preferred);
    const VkMemoryPropertyFlags selectedFlags =
        memoryProperties.memoryTypes[memoryType].propertyFlags;
    out.coherent = (selectedFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    requireVk(vkAllocateMemory(device, &allocation, nullptr, &out.memory),
              "vkAllocateMemory(buffer)");
    requireVk(vkBindBufferMemory(device, out.buffer, out.memory, 0u), "vkBindBufferMemory");
    if ((required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u) {
        requireVk(vkMapMemory(device, out.memory, 0u, size, 0u, &out.mapped), "vkMapMemory");
    }
    return out;
}

void VulkanRenderBackendImpl::destroyBuffer(Buffer& buffer) {
    if (device == VK_NULL_HANDLE) return;
    if (buffer.mapped != nullptr && buffer.memory != VK_NULL_HANDLE) {
        vkUnmapMemory(device, buffer.memory);
    }
    if (buffer.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

bool VulkanRenderBackendImpl::writeTransient(const void* data,
                                             VkDeviceSize size,
                                             VkDeviceSize alignment,
                                             VkBuffer& outBuffer,
                                             VkDeviceSize& outOffset) {
    if (!frameActive || data == nullptr || size == 0u) return false;
    Buffer& buffer = frames[currentFrame].transient;
    const VkDeviceSize safeAlignment = std::max<VkDeviceSize>(alignment, 1u);
    const VkDeviceSize aligned = (buffer.offset + safeAlignment - 1u) & ~(safeAlignment - 1u);
    if (aligned > buffer.size || size > buffer.size - aligned) {
        if (!transientOverflowLogged) {
            std::cerr << "[Vulkan] Transient frame buffer exhausted; skipping remaining oversized draws.\n";
            transientOverflowLogged = true;
        }
        return false;
    }
    std::memcpy(static_cast<std::byte*>(buffer.mapped) + aligned,
                data,
                static_cast<std::size_t>(size));
    if (!buffer.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory;
        range.offset = 0u;
        range.size = VK_WHOLE_SIZE;
        requireVk(vkFlushMappedMemoryRanges(device, 1u, &range),
                  "vkFlushMappedMemoryRanges");
    }
    buffer.offset = aligned + size;
    outBuffer = buffer.buffer;
    outOffset = aligned;
    return true;
}

VkCommandBuffer VulkanRenderBackendImpl::beginOneTimeCommands() {
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1u;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    requireVk(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer),
              "vkAllocateCommandBuffers(one-time)");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    requireVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
              "vkBeginCommandBuffer(one-time)");
    return commandBuffer;
}

void VulkanRenderBackendImpl::endOneTimeCommands(VkCommandBuffer commandBuffer) {
    requireVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(one-time)");
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &commandBuffer;
    requireVk(vkQueueSubmit(graphicsQueue, 1u, &submitInfo, VK_NULL_HANDLE),
              "vkQueueSubmit(one-time)");
    requireVk(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle(one-time)");
    vkFreeCommandBuffers(device, commandPool, 1u, &commandBuffer);
}

void VulkanRenderBackendImpl::shutdown() {
    frameActive = false;
    initialized = false;
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);

    if (device != VK_NULL_HANDLE) {
        destroyBuffer(screenshotReadback);
        destroyCachedWorldMeshes();
        indexedWorldMaterials.clear();
        worldMaterials.clear();
        worldSceneMaterialDescriptorSets.clear();
        worldSceneMaterials.clear();
        worldSceneMaterialCacheGeneration = 0u;
        for (auto& [_, texture] : worldTextures) destroyTexture(texture);
        for (auto& [_, texture] : spriteTextures) destroyTexture(texture);
        worldTextures.clear();
        spriteTextures.clear();
        destroyTexture(fallbackWorldTexture);
        destroyTexture(fallbackWorldNormalTexture);
        destroyTexture(fallbackWorldLinearTexture);
        destroyTexture(fallbackWorldEmissiveTexture);
        destroyEnvironmentResources();
        destroyTexture(fallbackSpriteTexture);
        fallbackWorldMaterial = {};
        destroySwapchainResources();

        for (FrameResources& frame : frames) {
            destroyBuffer(frame.transient);
            if (frame.timestampQueries != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device, frame.timestampQueries, nullptr);
            }
            if (frame.imageAvailable != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, frame.imageAvailable, nullptr);
            }
            if (frame.renderFinished != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, frame.renderFinished, nullptr);
            }
            if (frame.inFlight != VK_NULL_HANDLE) {
                vkDestroyFence(device, frame.inFlight, nullptr);
            }
            frame = {};
        }
        if (debugPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, debugPipelineLayout, nullptr);
        }
        if (texturedPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, texturedPipelineLayout, nullptr);
        }
        if (indirectWorldPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, indirectWorldPipelineLayout, nullptr);
        }
        if (worldSceneColorPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(
                device, worldSceneColorPipelineLayout, nullptr);
        }
        if (worldSceneColorSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, worldSceneColorSampler, nullptr);
        }
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        }
        if (textureSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, textureSetLayout, nullptr);
        }
        if (indexedWorldMaterialSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(
                device, indexedWorldMaterialSetLayout, nullptr);
        }
        if (worldStateSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, worldStateSetLayout, nullptr);
        }
        if (worldSceneColorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(
                device, worldSceneColorSetLayout, nullptr);
        }
        if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    device = VK_NULL_HANDLE;
    commandPool = VK_NULL_HANDLE;
    debugPipelineLayout = VK_NULL_HANDLE;
    texturedPipelineLayout = VK_NULL_HANDLE;
    indirectWorldPipelineLayout = VK_NULL_HANDLE;
    worldSceneColorPipelineLayout = VK_NULL_HANDLE;
    worldSceneColorSampler = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    textureSetLayout = VK_NULL_HANDLE;
    indexedWorldMaterialSetLayout = VK_NULL_HANDLE;
    worldStateSetLayout = VK_NULL_HANDLE;
    worldSceneColorSetLayout = VK_NULL_HANDLE;
    worldSceneColorDescriptorSets.fill(VK_NULL_HANDLE);
    descriptorIndexingSupported = false;
    indirectWorldBatchingSupported = false;
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    surface = VK_NULL_HANDLE;
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    window = nullptr;
}

void VulkanRenderBackendImpl::configureScreenshotCapture() {
    const auto path = engine::env::get("PAC_BACKEND_SCREENSHOT_PATH");
    if (!path.has_value() || path->empty()) return;
    screenshotPath = *path;
    screenshotCaptureConfigured = true;
    screenshotCaptured = false;
    screenshotFrameTarget = 0u;
    if (const auto target = engine::env::get("PAC_BACKEND_SCREENSHOT_FRAME")) {
        try {
            screenshotFrameTarget = static_cast<std::uint64_t>(std::stoull(*target));
        } catch (...) {
            screenshotFrameTarget = 0u;
        }
    }
}

bool VulkanRenderBackendImpl::recordScreenshotCopy(VkCommandBuffer commandBuffer) {
    if (!screenshotCaptureConfigured || screenshotCaptured || screenshotCopyPending ||
        frameCounter < screenshotFrameTarget) {
        return false;
    }
    if (!swapchainTransferSourceSupported) {
        std::cout << "[Screenshot][Vulkan] FAILED swapchain does not support transfer source usage\n";
        screenshotCaptured = true;
        return false;
    }
    screenshotWidth = swapchainExtent.width;
    screenshotHeight = swapchainExtent.height;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(screenshotWidth) *
                                   static_cast<VkDeviceSize>(screenshotHeight) * 4u;
    destroyBuffer(screenshotReadback);
    screenshotReadback = createBuffer(
        byteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = swapchainImages[acquiredImage];
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1u;
    toTransfer.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0u,
                         0u,
                         nullptr,
                         0u,
                         nullptr,
                         1u,
                         &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1u;
    copy.imageExtent = {screenshotWidth, screenshotHeight, 1u};
    vkCmdCopyImageToBuffer(commandBuffer,
                           swapchainImages[acquiredImage],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           screenshotReadback.buffer,
                           1u,
                           &copy);

    VkImageMemoryBarrier toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0u;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = swapchainImages[acquiredImage];
    toPresent.subresourceRange = toTransfer.subresourceRange;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0u,
                         0u,
                         nullptr,
                         0u,
                         nullptr,
                         1u,
                         &toPresent);
    screenshotCopyPending = true;
    return true;
}

void VulkanRenderBackendImpl::finishScreenshotCapture(VkFence frameFence) {
    if (!screenshotCopyPending || screenshotReadback.mapped == nullptr) return;
    requireVk(vkWaitForFences(device, 1u, &frameFence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences(screenshot)");
    if (!screenshotReadback.coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = screenshotReadback.memory;
        range.size = VK_WHOLE_SIZE;
        requireVk(vkInvalidateMappedMemoryRanges(device, 1u, &range),
                  "vkInvalidateMappedMemoryRanges(screenshot)");
    }
    const std::size_t byteCount = static_cast<std::size_t>(screenshotWidth) *
                                  static_cast<std::size_t>(screenshotHeight) * 4u;
    std::vector<unsigned char> rgba(byteCount);
    std::memcpy(rgba.data(), screenshotReadback.mapped, byteCount);
    if (swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
        swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
        for (std::size_t i = 0u; i < rgba.size(); i += 4u) {
            std::swap(rgba[i], rgba[i + 2u]);
        }
    }

    int wrote = 0;
    try {
        const std::filesystem::path outputPath(screenshotPath);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        wrote = stbi_write_png(outputPath.string().c_str(),
                               static_cast<int>(screenshotWidth),
                               static_cast<int>(screenshotHeight),
                               4,
                               rgba.data(),
                               static_cast<int>(screenshotWidth * 4u));
        std::cout << "[Screenshot][Vulkan] " << (wrote != 0 ? "WROTE " : "FAILED ")
                  << outputPath.string()
                  << " size=" << screenshotWidth << "x" << screenshotHeight
                  << " frame=" << frameCounter << "\n";
    } catch (const std::exception& exception) {
        std::cout << "[Screenshot][Vulkan] FAILED exception=" << exception.what() << "\n";
    }
    screenshotCaptured = true;
    screenshotCopyPending = false;
    destroyBuffer(screenshotReadback);
}
