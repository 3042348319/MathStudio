// ====================================================================
// viewport_renderer.cpp —— 子窗口 Vulkan 渲染器实现
// ====================================================================

// 与 window.h 保持一致: 使用 Vulkan 的深度范围 [0, 1]。
// 必须在包含 glm 之前定义, 否则 glm::perspective 会按 OpenGL [-1, 1] 生成,
// 导致网格地板反投影和深度测试计算出错。
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "viewport_renderer.h"

#include "shader_loader.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kMaxFramesInFlight = 2;

// 相机统一缓冲 (与 object.vert / grid.frag 中的 Camera 块一致)
struct CameraUBO {
    alignas(16) glm::mat4 view{1.0f};
    alignas(16) glm::mat4 proj{1.0f};
    alignas(16) glm::mat4 invViewProj{1.0f};
    alignas(16) glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    alignas(16) glm::vec4 screenSize{1.0f, 1.0f, 0.0f, 0.0f};
    alignas(16) glm::vec4 screenOffset{0.0f, 0.0f, 0.0f, 0.0f};
};

// 每个对象实例的 push constant (与 object.vert 中的 PushConstants 一致)
struct PushConstants {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

static_assert(sizeof(CameraUBO) == 240, "CameraUBO layout mismatch");
static_assert(sizeof(PushConstants) == 80, "push constant layout mismatch");

void checkVk(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("Vulkan 调用失败: ") + what);
    }
}

bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT;
}

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SwapchainSupport querySwapchainSupport(VkPhysicalDevice device,
                                        VkSurfaceKHR surface) {
    SwapchainSupport support;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device, surface, &support.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount > 0) {
        support.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &formatCount, support.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        support.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &presentModeCount, support.presentModes.data());
    }

    return support;
}

VkSurfaceFormatKHR chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(
    const std::vector<VkPresentModeKHR>& presentModes) {
    for (VkPresentModeKHR mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                        GLFWwindow* window) {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D extent{
        static_cast<uint32_t>(std::max(width, 1)),
        static_cast<uint32_t>(std::max(height, 1))};

    extent.width = std::clamp(extent.width,
                              capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height,
                               capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return extent;
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(
    const VkSurfaceCapabilitiesKHR& capabilities) {
    const VkCompositeAlphaFlagBitsKHR candidates[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (auto candidate : candidates) {
        if (capabilities.supportedCompositeAlpha & candidate) {
            return candidate;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

}  // namespace

glm::vec3 viewportCameraPosition(const ViewportCamera& camera) {
    const float cosPitch = std::cos(camera.pitch);
    return camera.target +
           camera.distance * glm::vec3(cosPitch * std::cos(camera.yaw),
                                       cosPitch * std::sin(camera.yaw),
                                       std::sin(camera.pitch));
}

glm::vec3 viewportCameraUp(const ViewportCamera& camera) {
    const glm::vec3 eye = viewportCameraPosition(camera);
    const glm::vec3 forward = glm::normalize(camera.target - eye);
    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);

    // 视线与 Z 轴接近平行时, 用 Y 轴作为屏幕上方。
    if (std::abs(glm::dot(forward, worldUp)) > 0.9995f) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    const glm::vec3 right = glm::normalize(
        glm::cross(forward, worldUp));
    return glm::normalize(glm::cross(right, forward));
}

glm::mat4 viewportCameraProjection(const ViewportCamera& camera,
                                   float aspect) {
    if (!camera.orthographic) {
        glm::mat4 projection = glm::perspective(
            camera.fov, aspect, camera.nearPlane, camera.farPlane);
        projection[1][1] *= -1.0f;
        return projection;
    }

    // 保持和透视相机在 target 处相同的可见高度。
    const float halfHeight = std::max(
        0.01f, camera.distance *
                   std::tan(camera.fov * 0.5f));
    const float halfWidth = halfHeight * std::max(aspect, 0.01f);
    glm::mat4 projection = glm::ortho(
        -halfWidth, halfWidth, -halfHeight, halfHeight,
        camera.nearPlane, camera.farPlane);
    projection[1][1] *= -1.0f;
    return projection;
}

// ====================================================================
// ViewportRenderer::Impl
// ====================================================================

struct ViewportRenderer::Impl {
    std::shared_ptr<const ViewportContext> context;
    GLFWwindow* window = nullptr;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    VkFormat swapChainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapChainExtent{0, 0};
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    // 每次 createRenderPass() 递增; 子窗口据此判断 ImGui 后端是否过期。
    uint32_t renderPassVersion = 0;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline outlinePipeline = VK_NULL_HANDLE;
    VkPipeline transparentPipeline = VK_NULL_HANDLE;
    VkPipeline gridPipeline = VK_NULL_HANDLE;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

    size_t currentFrame = 0;
    bool framebufferResized = false;

    // 每个 Mesh 上传一次, 后续所有实例复用同一组顶点/索引缓冲。
    struct GpuMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };
    std::unordered_map<const Mesh*, GpuMesh> meshCache;

    Impl(std::shared_ptr<const ViewportContext> sharedContext,
         GLFWwindow* glfwWindow)
        : context(std::move(sharedContext)), window(glfwWindow) {
        if (!context || !window) {
            throw std::invalid_argument("ViewportRenderer: invalid context/window");
        }
        if (context->device == VK_NULL_HANDLE || context->instance == VK_NULL_HANDLE) {
            throw std::invalid_argument("ViewportRenderer: Vulkan context not ready");
        }

        try {
            checkVk(glfwCreateWindowSurface(context->instance, window,
                                            nullptr, &surface),
                    "glfwCreateWindowSurface");
            createDescriptorSetLayout();
            createSwapChainResources();
        } catch (...) {
            destroy();
            throw;
        }
    }

    ~Impl() {
        destroy();
    }

    // ----------------------------------------------------------------
    // 生命周期
    // ----------------------------------------------------------------

    void waitIdle() {
        if (context && context->device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(context->device);
        }
    }

    void destroy() {
        waitIdle();
        cleanupSwapChain();
        destroyMeshCache();

        if (descriptorSetLayout != VK_NULL_HANDLE &&
            context && context->device != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(context->device, descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
        }
        if (surface != VK_NULL_HANDLE && context &&
            context->instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(context->instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
    }

    void cleanupSwapChain() {
        if (!context || context->device == VK_NULL_HANDLE) {
            return;
        }
        VkDevice device = context->device;

        for (VkSemaphore semaphore : imageAvailableSemaphores) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        imageAvailableSemaphores.clear();

        for (VkSemaphore semaphore : renderFinishedSemaphores) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        renderFinishedSemaphores.clear();

        for (VkFence fence : inFlightFences) {
            vkDestroyFence(device, fence, nullptr);
        }
        inFlightFences.clear();
        imagesInFlight.clear();

        if (!commandBuffers.empty() && context->commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, context->commandPool,
                                 static_cast<uint32_t>(commandBuffers.size()),
                                 commandBuffers.data());
        }
        commandBuffers.clear();

        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
            descriptorSets.clear();
        }

        for (size_t i = 0; i < uniformBuffers.size(); ++i) {
            vkDestroyBuffer(device, uniformBuffers[i], nullptr);
            vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
        }
        uniformBuffers.clear();
        uniformBuffersMemory.clear();

        for (VkFramebuffer framebuffer : framebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        framebuffers.clear();

        if (depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depthImageView, nullptr);
            depthImageView = VK_NULL_HANDLE;
        }
        if (depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, depthImage, nullptr);
            depthImage = VK_NULL_HANDLE;
        }
        if (depthImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, depthImageMemory, nullptr);
            depthImageMemory = VK_NULL_HANDLE;
        }

        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (outlinePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, outlinePipeline, nullptr);
            outlinePipeline = VK_NULL_HANDLE;
        }
        if (transparentPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, transparentPipeline, nullptr);
            transparentPipeline = VK_NULL_HANDLE;
        }
        if (gridPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, gridPipeline, nullptr);
            gridPipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        for (VkImageView imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        swapChainImageViews.clear();
        swapChainImages.clear();

        if (swapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapChain, nullptr);
            swapChain = VK_NULL_HANDLE;
        }
    }

    void recreateSwapChain() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window, &width, &height);
        }

        waitIdle();
        cleanupSwapChain();
        createSwapChainResources();
    }

    // ----------------------------------------------------------------
    // 资源创建
    // ----------------------------------------------------------------

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        // 对象顶点着色器需要 view/proj; 网格片元着色器需要 invViewProj/
        // cameraPosition/screenSize, 因此 binding 0 必须同时可见于两个阶段。
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings = &binding;

        checkVk(vkCreateDescriptorSetLayout(context->device, &info, nullptr,
                                            &descriptorSetLayout),
                "vkCreateDescriptorSetLayout");
    }

    void createSwapChainResources() {
        createSwapChain();
        createImageViews();
        createRenderPass();
        createPipelineLayout();
        createGraphicsPipeline();
        createGridPipeline();
        createDepthResources();
        createFramebuffers();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();

        currentFrame = 0;
        framebufferResized = false;
    }

    void createSwapChain() {
        SwapchainSupport support =
            querySwapchainSupport(context->physicalDevice, surface);
        if (support.formats.empty() || support.presentModes.empty()) {
            throw std::runtime_error("子窗口交换链不支持任何格式/呈现模式!");
        }

        const VkSurfaceFormatKHR surfaceFormat =
            chooseSurfaceFormat(support.formats);
        const VkPresentModeKHR presentMode =
            choosePresentMode(support.presentModes);
        const VkExtent2D extent = chooseExtent(support.capabilities, window);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 &&
            imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = surfaceFormat.format;
        info.imageColorSpace = surfaceFormat.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const uint32_t families[] = {
            context->graphicsFamily, context->presentFamily};
        if (context->graphicsFamily != context->presentFamily) {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = families;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.queueFamilyIndexCount = 0;
            info.pQueueFamilyIndices = nullptr;
        }

        info.preTransform = support.capabilities.currentTransform;
        info.compositeAlpha = chooseCompositeAlpha(support.capabilities);
        info.presentMode = presentMode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = VK_NULL_HANDLE;

        checkVk(vkCreateSwapchainKHR(context->device, &info, nullptr, &swapChain),
                "vkCreateSwapchainKHR");

        vkGetSwapchainImagesKHR(context->device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(context->device, swapChain, &imageCount,
                                swapChainImages.data());

        swapChainFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    void createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); ++i) {
            swapChainImageViews[i] = createImageView(
                swapChainImages[i], swapChainFormat,
                VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = context->depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // 深度图像在 createDepthResources 中已经转换到该布局
        depthAttachment.initialLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const std::array<VkAttachmentDescription, 2> attachments = {
            colorAttachment, depthAttachment};

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        checkVk(vkCreateRenderPass(context->device, &info, nullptr, &renderPass),
                "vkCreateRenderPass");

        // renderPass 句柄变了, 绑定在它上面的 ImGui 管线必须重建。
        ++renderPassVersion;
    }

    void createPipelineLayout() {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = 1;
        info.pSetLayouts = &descriptorSetLayout;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &pushRange;

        checkVk(vkCreatePipelineLayout(context->device, &info, nullptr,
                                       &pipelineLayout),
                "vkCreatePipelineLayout");
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) const {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        checkVk(vkCreateShaderModule(context->device, &info, nullptr, &module),
                "vkCreateShaderModule");
        return module;
    }

    void createGraphicsPipeline() {
        const std::vector<char> vertCode = loadShaderCode("object_vert.spv");
        const std::vector<char> fragCode = loadShaderCode("object_frag.spv");

        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        const VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription binding = Mesh::bindingDescription();
        std::array<VkVertexInputAttributeDescription, 3> attributes =
            Mesh::attributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        // object.vert 只使用位置和颜色; 纹理坐标仍保留在 Mesh 布局中,
        // 但这里不声明 location 2, 避免"顶点属性未被着色器使用"的验证警告。
        vertexInput.vertexAttributeDescriptionCount = 2;
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        // 共享立方体的所有面现在都按外侧逆时针排列, 可以正常背面剔除。
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorAttachment{};
        colorAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorAttachment;

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &colorBlending;
        info.pDynamicState = &dynamicState;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        info.subpass = 0;
        info.basePipelineHandle = VK_NULL_HANDLE;
        info.basePipelineIndex = -1;

        // 同一次管线创建流程生成两个变体:
        //   1. 正常物体: 剔除背面, 写深度;
        //   2. 选中描边: 剔除正面, 不写深度, 只画放大后的背面。
        // 描边在正常物体之后绘制, 深度测试会挡住物体内部,
        // 只在轮廓外侧露出黑色边缘。
        const auto buildPipeline =
            [&](VkCullModeFlags cullMode,
                VkBool32 depthWrite,
                VkCompareOp depthCompare,
                bool blend,
                VkPipeline& target) {
                rasterizer.cullMode = cullMode;
                depthStencil.depthWriteEnable = depthWrite;
                depthStencil.depthCompareOp = depthCompare;

                colorAttachment.blendEnable = blend ? VK_TRUE : VK_FALSE;
                if (blend) {
                    colorAttachment.srcColorBlendFactor =
                        VK_BLEND_FACTOR_SRC_ALPHA;
                    colorAttachment.dstColorBlendFactor =
                        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                    colorAttachment.srcAlphaBlendFactor =
                        VK_BLEND_FACTOR_ONE;
                    colorAttachment.dstAlphaBlendFactor =
                        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
                }

                checkVk(vkCreateGraphicsPipelines(
                            context->device, VK_NULL_HANDLE,
                            1, &info, nullptr, &target),
                        "vkCreateGraphicsPipelines");
            };

        buildPipeline(VK_CULL_MODE_BACK_BIT, VK_TRUE,
                      VK_COMPARE_OP_LESS, false, pipeline);
        buildPipeline(VK_CULL_MODE_FRONT_BIT, VK_FALSE,
                      VK_COMPARE_OP_LESS_OR_EQUAL, false,
                      outlinePipeline);
        buildPipeline(VK_CULL_MODE_NONE, VK_FALSE,
                      VK_COMPARE_OP_LESS_OR_EQUAL, true,
                      transparentPipeline);

        vkDestroyShaderModule(context->device, fragModule, nullptr);
        vkDestroyShaderModule(context->device, vertModule, nullptr);
    }

    void createGridPipeline() {
        const std::vector<char> vertCode = loadShaderCode("grid_vert.spv");
        const std::vector<char> fragCode = loadShaderCode("grid_frag.spv");

        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        const VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorAttachment{};
        colorAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorAttachment;

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &colorBlending;
        info.pDynamicState = &dynamicState;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        info.subpass = 0;

        checkVk(vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE,
                                          1, &info, nullptr, &gridPipeline),
                "vkCreateGraphicsPipelines(grid)");

        vkDestroyShaderModule(context->device, fragModule, nullptr);
        vkDestroyShaderModule(context->device, vertModule, nullptr);
    }

    void createDepthResources() {
        checkVk(createImage(swapChainExtent.width, swapChainExtent.height,
                            context->depthFormat,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            depthImage, depthImageMemory),
                "create depth image");

        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(context->depthFormat)) {
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        depthImageView = createImageView(depthImage, context->depthFormat, aspect);

        transitionImageLayout(
            depthImage, context->depthFormat,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT |
                (hasStencilComponent(context->depthFormat)
                     ? VK_IMAGE_ASPECT_STENCIL_BIT
                     : 0));
    }

    void createFramebuffers() {
        framebuffers.resize(swapChainImageViews.size());
        for (size_t i = 0; i < swapChainImageViews.size(); ++i) {
            const std::array<VkImageView, 2> attachments = {
                swapChainImageViews[i], depthImageView};

            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = renderPass;
            info.attachmentCount = static_cast<uint32_t>(attachments.size());
            info.pAttachments = attachments.data();
            info.width = swapChainExtent.width;
            info.height = swapChainExtent.height;
            info.layers = 1;

            checkVk(vkCreateFramebuffer(context->device, &info, nullptr,
                                        &framebuffers[i]),
                    "vkCreateFramebuffer");
        }
    }

    void createUniformBuffers() {
        uniformBuffers.resize(swapChainImages.size());
        uniformBuffersMemory.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); ++i) {
            checkVk(createBuffer(sizeof(CameraUBO),
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 uniformBuffers[i], uniformBuffersMemory[i]),
                    "create camera UBO");
        }
    }

    void createDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<uint32_t>(swapChainImages.size());

        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.poolSizeCount = 1;
        info.pPoolSizes = &poolSize;
        info.maxSets = static_cast<uint32_t>(swapChainImages.size());

        checkVk(vkCreateDescriptorPool(context->device, &info, nullptr,
                                       &descriptorPool),
                "vkCreateDescriptorPool");
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(
            swapChainImages.size(), descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount =
            static_cast<uint32_t>(swapChainImages.size());
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(swapChainImages.size());
        checkVk(vkAllocateDescriptorSets(context->device, &allocInfo,
                                         descriptorSets.data()),
                "vkAllocateDescriptorSets");

        for (size_t i = 0; i < swapChainImages.size(); ++i) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CameraUBO);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets[i];
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(context->device, 1, &write, 0, nullptr);
        }
    }

    void createCommandBuffers() {
        commandBuffers.resize(swapChainImages.size());

        VkCommandBufferAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = context->commandPool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount =
            static_cast<uint32_t>(commandBuffers.size());

        checkVk(vkAllocateCommandBuffers(context->device, &info,
                                         commandBuffers.data()),
                "vkAllocateCommandBuffers");
    }

    void createSyncObjects() {
        imageAvailableSemaphores.resize(kMaxFramesInFlight);
        renderFinishedSemaphores.resize(swapChainImages.size());
        inFlightFences.resize(kMaxFramesInFlight);
        imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
            checkVk(vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                                      &imageAvailableSemaphores[i]),
                    "vkCreateSemaphore(imageAvailable)");
            checkVk(vkCreateFence(context->device, &fenceInfo, nullptr,
                                  &inFlightFences[i]),
                    "vkCreateFence");
        }
        for (size_t i = 0; i < swapChainImages.size(); ++i) {
            checkVk(vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                                      &renderFinishedSemaphores[i]),
                    "vkCreateSemaphore(renderFinished)");
        }
    }

    // ----------------------------------------------------------------
    // 通用 Vulkan 辅助
    // ----------------------------------------------------------------

    uint32_t findMemoryType(uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(context->physicalDevice,
                                            &memoryProperties);

        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & properties) ==
                    properties) {
                return i;
            }
        }
        throw std::runtime_error("找不到合适的内存类型!");
    }

    VkResult createImage(uint32_t width, uint32_t height,
                         VkFormat format, VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage& image, VkDeviceMemory& memory) const {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = tiling;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = usage;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateImage(context->device, &info, nullptr, &image);
        if (result != VK_SUCCESS) {
            return result;
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(context->device, image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex =
            findMemoryType(requirements.memoryTypeBits, properties);

        result = vkAllocateMemory(context->device, &allocInfo, nullptr, &memory);
        if (result != VK_SUCCESS) {
            return result;
        }

        return vkBindImageMemory(context->device, image, memory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspect) const {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.subresourceRange.aspectMask = aspect;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;

        VkImageView view = VK_NULL_HANDLE;
        checkVk(vkCreateImageView(context->device, &info, nullptr, &view),
                "vkCreateImageView");
        return view;
    }

    VkResult createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer& buffer, VkDeviceMemory& memory) const {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(context->device, &info, nullptr, &buffer);
        if (result != VK_SUCCESS) {
            return result;
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(context->device, buffer, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex =
            findMemoryType(requirements.memoryTypeBits, properties);

        result = vkAllocateMemory(context->device, &allocInfo, nullptr, &memory);
        if (result != VK_SUCCESS) {
            return result;
        }

        return vkBindBufferMemory(context->device, buffer, memory, 0);
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer,
                    VkDeviceSize size) const {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    GpuMesh& getOrCreateGpuMesh(const Mesh& mesh) {
        const auto found = meshCache.find(&mesh);
        if (found != meshCache.end()) {
            return found->second;
        }
        if (mesh.empty()) {
            throw std::runtime_error("不能上传空网格!");
        }

        GpuMesh gpu{};
        const VkDeviceSize vertexSize = mesh.vertexDataSize();
        const VkDeviceSize indexSize =
            static_cast<VkDeviceSize>(mesh.indices().size() * sizeof(uint32_t));

        // ---- 顶点: 暂存缓冲 -> 设备本地缓冲 ----
        VkBuffer vertexStaging = VK_NULL_HANDLE;
        VkDeviceMemory vertexStagingMemory = VK_NULL_HANDLE;
        checkVk(createBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             vertexStaging, vertexStagingMemory),
                "create vertex staging buffer");

        void* mapped = nullptr;
        checkVk(vkMapMemory(context->device, vertexStagingMemory, 0,
                            vertexSize, 0, &mapped),
                "map vertex staging buffer");
        std::memcpy(mapped, mesh.vertexData(),
                    static_cast<std::size_t>(vertexSize));
        vkUnmapMemory(context->device, vertexStagingMemory);

        checkVk(createBuffer(vertexSize,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             gpu.vertexBuffer, gpu.vertexMemory),
                "create vertex buffer");
        copyBuffer(vertexStaging, gpu.vertexBuffer, vertexSize);

        vkDestroyBuffer(context->device, vertexStaging, nullptr);
        vkFreeMemory(context->device, vertexStagingMemory, nullptr);

        // ---- 索引: 暂存缓冲 -> 设备本地缓冲 ----
        VkBuffer indexStaging = VK_NULL_HANDLE;
        VkDeviceMemory indexStagingMemory = VK_NULL_HANDLE;
        checkVk(createBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             indexStaging, indexStagingMemory),
                "create index staging buffer");

        mapped = nullptr;
        checkVk(vkMapMemory(context->device, indexStagingMemory, 0,
                            indexSize, 0, &mapped),
                "map index staging buffer");
        std::memcpy(mapped, mesh.indices().data(),
                    static_cast<std::size_t>(indexSize));
        vkUnmapMemory(context->device, indexStagingMemory);

        checkVk(createBuffer(indexSize,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             gpu.indexBuffer, gpu.indexMemory),
                "create index buffer");
        copyBuffer(indexStaging, gpu.indexBuffer, indexSize);

        vkDestroyBuffer(context->device, indexStaging, nullptr);
        vkFreeMemory(context->device, indexStagingMemory, nullptr);

        gpu.indexCount = mesh.indexCount();
        const auto inserted = meshCache.emplace(&mesh, gpu);
        return inserted.first->second;
    }

    void destroyMeshCache() {
        if (!context || context->device == VK_NULL_HANDLE) {
            return;
        }
        for (auto& entry : meshCache) {
            GpuMesh& gpu = entry.second;
            if (gpu.vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(context->device, gpu.vertexBuffer, nullptr);
            }
            if (gpu.vertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(context->device, gpu.vertexMemory, nullptr);
            }
            if (gpu.indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(context->device, gpu.indexBuffer, nullptr);
            }
            if (gpu.indexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(context->device, gpu.indexMemory, nullptr);
            }
        }
        meshCache.clear();
    }

    VkCommandBuffer beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandPool = context->commandPool;
        info.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        checkVk(vkAllocateCommandBuffers(context->device, &info, &commandBuffer),
                "vkAllocateCommandBuffers(single)");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(context->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(context->graphicsQueue);

        vkFreeCommandBuffers(context->device, context->commandPool, 1,
                             &commandBuffer);
    }

    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               VkImageAspectFlags aspect) const {
        (void)format;
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    // ----------------------------------------------------------------
    // 每帧渲染
    // ----------------------------------------------------------------

    void updateCamera(uint32_t imageIndex, const ViewportCamera& camera,
                      const VkRect2D& sceneViewport) {
        CameraUBO ubo{};
        const glm::vec3 eye = viewportCameraPosition(camera);
        ubo.view = glm::lookAt(eye, camera.target,
                               viewportCameraUp(camera));
        const float aspect =
            sceneViewport.extent.height == 0
                ? 1.0f
                : static_cast<float>(sceneViewport.extent.width) /
                      static_cast<float>(sceneViewport.extent.height);
        ubo.proj = viewportCameraProjection(camera, aspect);
        ubo.invViewProj = glm::inverse(ubo.proj * ubo.view);
        ubo.cameraPosition = glm::vec4(eye, 1.0f);
        ubo.screenSize = glm::vec4(
            static_cast<float>(sceneViewport.extent.width),
            static_cast<float>(sceneViewport.extent.height), 0.0f, 0.0f);
        ubo.screenOffset = glm::vec4(
            static_cast<float>(sceneViewport.offset.x),
            static_cast<float>(sceneViewport.offset.y), 0.0f, 0.0f);

        void* data = nullptr;
        checkVk(vkMapMemory(context->device, uniformBuffersMemory[imageIndex],
                            0, sizeof(ubo), 0, &data),
                "vkMapMemory(camera)");
        std::memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(context->device, uniformBuffersMemory[imageIndex]);
    }

    /**
     * 计算选中物体的描边变换
     *
     * 不能直接给实例矩阵乘一个统一的 1.06 缩放:
     * 坐标系的轴是很长的细长盒, 统一缩放会把长度放大很多,
     * 导致轴端出现大块黑色盖子。
     *
     * 这里改为:
     *   1. 计算实例在世界空间的 AABB;
     *   2. 在 AABB 的每个方向上固定外扩 margin;
     *   3. 围绕 AABB 中心缩放回原变换。
     *
     * 这样立方体和细长坐标轴都能得到大致均匀的黑色边框。
     */
    glm::mat4 makeOutlineTransform(const Mesh& mesh,
                                   const glm::mat4& transform,
                                   float margin) const {
        if (mesh.vertices().empty()) {
            return transform;
        }

        glm::vec3 minPoint(std::numeric_limits<float>::max());
        glm::vec3 maxPoint(-std::numeric_limits<float>::max());

        for (const MeshVertex& vertex : mesh.vertices()) {
            const glm::vec3 world = glm::vec3(
                transform * glm::vec4(vertex.position, 1.0f));
            minPoint = glm::min(minPoint, world);
            maxPoint = glm::max(maxPoint, world);
        }

        const glm::vec3 center = (minPoint + maxPoint) * 0.5f;
        const glm::vec3 size = maxPoint - minPoint;
        const glm::vec3 safeSize = glm::max(size, glm::vec3(1e-4f));
        const glm::vec3 expanded = size + glm::vec3(2.0f * margin);

        return glm::translate(glm::mat4(1.0f), center) *
               glm::scale(glm::mat4(1.0f), expanded / safeSize) *
               glm::translate(glm::mat4(1.0f), -center) *
               transform;
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer,
                             uint32_t imageIndex,
                             const std::vector<ObjectInstance>& instances,
                             ImDrawData* overlay,
                             const std::vector<ObjectInstance>* outline,
                             const VkRect2D& sceneViewport) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;
        checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                "vkBeginCommandBuffer");

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChainExtent;

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.03f, 0.035f, 0.06f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount =
            static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        // 绑定当前交换链图像对应的相机 UBO (view/proj/invViewProj 等)
        const VkDescriptorSet descriptorSet = descriptorSets[imageIndex];
        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, &descriptorSet,
                                0, nullptr);

        VkViewport viewport{};
        viewport.x = static_cast<float>(sceneViewport.offset.x);
        viewport.y = static_cast<float>(sceneViewport.offset.y);
        viewport.width = static_cast<float>(sceneViewport.extent.width);
        viewport.height = static_cast<float>(sceneViewport.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor = sceneViewport;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // 先画无限网格地板 (全屏三角形, 深度测试关闭), 再画对象。
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          gridPipeline);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        // ---- 选中物体的黑色描边 ----
        // 先画放大的黑色外壳, 再画正常物体。
        // 外壳不写深度, 因此后续正常物体一定会覆盖它;
        // 只有物体轮廓外侧没有被覆盖的黑色部分会保留下来。
        // 这样坐标轴在原点重叠时不会出现内部黑色斑块。
        if (outline != nullptr && !outline->empty()) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              outlinePipeline);

            for (const ObjectInstance& instance : *outline) {
                if (instance.mesh == nullptr || instance.mesh->empty() ||
                    instance.transparent) {
                    continue;
                }

                GpuMesh& gpu = getOrCreateGpuMesh(*instance.mesh);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(commandBuffer, 0, 1,
                                       &gpu.vertexBuffer, &offset);
                vkCmdBindIndexBuffer(commandBuffer, gpu.indexBuffer, 0,
                                     VK_INDEX_TYPE_UINT32);

                PushConstants push{};
                push.model = makeOutlineTransform(
                    *instance.mesh, instance.transform, 0.015f);
                push.color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

                vkCmdPushConstants(commandBuffer, pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(push), &push);
                vkCmdDrawIndexed(commandBuffer, gpu.indexCount, 1, 0, 0, 0);
            }
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline);

        for (const ObjectInstance& instance : instances) {
            if (instance.mesh == nullptr || instance.mesh->empty() ||
                instance.transparent) {
                continue;
            }

            GpuMesh& gpu = getOrCreateGpuMesh(*instance.mesh);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1,
                                   &gpu.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(commandBuffer, gpu.indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);

            PushConstants push{};
            push.model = instance.transform;
            push.color = instance.color;

            vkCmdPushConstants(commandBuffer, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(push), &push);
            vkCmdDrawIndexed(commandBuffer, gpu.indexCount, 1, 0, 0, 0);
        }

        // ---- 半透明轨迹 ----
        // 普通物体绘制完成后再画透明轨迹, 保证混合结果稳定。
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          transparentPipeline);

        for (const ObjectInstance& instance : instances) {
            if (instance.mesh == nullptr || instance.mesh->empty() ||
                !instance.transparent) {
                continue;
            }

            GpuMesh& gpu = getOrCreateGpuMesh(*instance.mesh);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1,
                                   &gpu.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(commandBuffer, gpu.indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);

            PushConstants push{};
            push.model = instance.transform;
            push.color = instance.color;

            vkCmdPushConstants(commandBuffer, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(push), &push);
            vkCmdDrawIndexed(commandBuffer, gpu.indexCount, 1, 0, 0, 0);
        }

        // ImGui 使用整个交换链, 因此恢复完整视口/裁剪区域。
        VkViewport fullViewport{};
        fullViewport.x = 0.0f;
        fullViewport.y = 0.0f;
        fullViewport.width = static_cast<float>(swapChainExtent.width);
        fullViewport.height = static_cast<float>(swapChainExtent.height);
        fullViewport.minDepth = 0.0f;
        fullViewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &fullViewport);

        VkRect2D fullScissor{};
        fullScissor.offset = {0, 0};
        fullScissor.extent = swapChainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);

        // 3D 场景之后绘制 ImGui 浮窗; 浮窗总是在最上层。
        if (overlay != nullptr && overlay->CmdListsCount > 0) {
            ImGui_ImplVulkan_RenderDrawData(overlay, commandBuffer);
        }

        vkCmdEndRenderPass(commandBuffer);
        checkVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    }

    bool ensureSwapChainReady() {
        if (swapChain == VK_NULL_HANDLE) {
            return false;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) {
            return false;
        }

        if (framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        }
        return true;
    }

    void draw(const std::vector<ObjectInstance>& instances,
              const ViewportCamera& camera,
              ImDrawData* overlay,
              const std::vector<ObjectInstance>* outline,
              VkRect2D sceneViewport) {
        if (!ensureSwapChainReady()) {
            return;
        }

        // 空矩形或尺寸为 0 时使用完整交换链。
        if (sceneViewport.extent.width == 0 ||
            sceneViewport.extent.height == 0) {
            sceneViewport.offset = {0, 0};
            sceneViewport.extent = swapChainExtent;
        }

        vkWaitForFences(context->device, 1, &inFlightFences[currentFrame],
                        VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(
            context->device, swapChain, UINT64_MAX,
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE,
            &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("子窗口获取交换链图像失败!");
        }

        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(context->device, 1, &imagesInFlight[imageIndex],
                            VK_TRUE, UINT64_MAX);
        }
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        updateCamera(imageIndex, camera, sceneViewport);

        vkResetFences(context->device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[imageIndex], 0);
        recordCommandBuffer(commandBuffers[imageIndex], imageIndex, instances,
                            overlay, outline, sceneViewport);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        const VkSemaphore waitSemaphores[] = {
            imageAvailableSemaphores[currentFrame]};
        const VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

        const VkSemaphore signalSemaphores[] = {
            renderFinishedSemaphores[imageIndex]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        checkVk(vkQueueSubmit(context->graphicsQueue, 1, &submitInfo,
                              inFlightFences[currentFrame]),
                "vkQueueSubmit(viewport)");

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        const VkSwapchainKHR swapChains[] = {swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(context->presentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("子窗口呈现失败!");
        }

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
    }
};

// ====================================================================
// ViewportRenderer 公共接口
// ====================================================================

ViewportRenderer::ViewportRenderer(
    std::shared_ptr<const ViewportContext> context,
    GLFWwindow* window)
    : impl_(std::make_unique<Impl>(std::move(context), window)) {}

ViewportRenderer::~ViewportRenderer() = default;

void ViewportRenderer::draw(const std::vector<ObjectInstance>& instances,
                            const ViewportCamera& camera,
                            ImDrawData* overlay,
                            const std::vector<ObjectInstance>* outline,
                            VkRect2D sceneViewport) {
    impl_->draw(instances, camera, overlay, outline, sceneViewport);
}

bool ViewportRenderer::ensureSwapChainReady() {
    return impl_->ensureSwapChainReady();
}

VkRenderPass ViewportRenderer::renderPass() const {
    return impl_->renderPass;
}

uint32_t ViewportRenderer::imageCount() const {
    return static_cast<uint32_t>(impl_->swapChainImages.size());
}

uint32_t ViewportRenderer::renderPassVersion() const {
    return impl_->renderPassVersion;
}

void ViewportRenderer::onFramebufferResized() {
    impl_->framebufferResized = true;
}

void ViewportRenderer::waitIdle() {
    impl_->waitIdle();
}
