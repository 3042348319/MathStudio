// ====================================================================
// Vulkan 完整示例 —— 窗口与应用程序类实现 (window.cpp)
// 本文件实现 window.h 中声明的工具函数、辅助结构体成员函数以及
// VulkanTutorialApp 的全部成员函数。
// 函数功能、参数与返回值说明见 window.h 中对应的声明注释;
// 本文件保留每个实现步骤的详细注释。
// ====================================================================

#include "window.h"

#include "app_icon.h"
#include "dynamic_plugin_manager.h"
#include "menu.h"
#include "settings.h"
#include "shader_loader.h"
#include "theme.h"
#include "viewport_context.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <algorithm>  // std::max / std::min / std::clamp
#include <chrono>     // std::chrono (计算旋转动画时间)
#include <cmath>      // std::floor / std::log2 (计算 Mipmap 级数)
#include <cstddef>    // offsetof
#include <cstdlib>    // std::getenv (桌面路径)
#include <cstring>    // memcpy
#include <filesystem> // 检查中文字体文件是否存在
#include <iostream>   // 错误输出
#include <map>        // std::multimap (按分数排序物理设备)
#include <set>        // std::set (去重队列族)
#include <stdexcept>  // std::runtime_error

// ====================================================================
// 二、着色器读取
// ====================================================================
// loadShaderCode 的实现已移到 shader_loader.cpp, 主窗口和子窗口渲染器共用。

// ====================================================================
// 三、自定义数据结构成员函数
// ====================================================================

bool QueueFamilyIndices::isComplete() const {
    return graphicsFamily.has_value() && presentFamily.has_value();
}

QueueFamilyIndices QueueFamilyIndices::findQueueFamilies(
    VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    // 第一步: 枚举该物理设备上的所有队列族
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // 第二步: 逐个检查每个队列族的功能
    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        // VK_QUEUE_GRAPHICS_BIT: 该族支持图形命令
        // (绘制所需的 vkCmdDraw 等命令都要提交到图形队列)
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        // vkGetPhysicalDeviceSurfaceSupportKHR: 检查该族能否向
        // 我们的窗口表面呈现图像 (与操作系统显示系统对接)
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        // 两个都找到了就可以提前退出
        if (indices.isComplete())
            break;
        i++;
    }
    return indices;
}

SwapChainSupportDetails SwapChainSupportDetails::querySwapChainSupport(
    VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;

    // 获取表面能力 (交换链最少/最多图像数, 表面当前尺寸等)
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    // 获取支持的表面格式 (VkSurfaceFormatKHR = 像素格式 + 颜色空间)
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &formatCount, details.formats.data());
    }

    // 获取支持的呈现模式 (决定画面如何送到屏幕)
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkVertexInputBindingDescription Vertex::getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;  // 绑定索引 0 (对应 vkCmdBindVertexBuffers 的第一个槽位)
    bindingDescription.stride = sizeof(Vertex);  // 相邻两个顶点之间的字节步长
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;  // 每个顶点移动一步
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    // 属性 0: 顶点位置, 3 个 32 位浮点 (vec3)
    attributeDescriptions[0].binding = 0;                 // 来自绑定 0
    attributeDescriptions[0].location = 0;                // 对应着色器 location = 0
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;  // 3×float
    attributeDescriptions[0].offset = offsetof(Vertex, pos);       // 结构体内偏移

    // 属性 1: 顶点颜色, 3 个 32 位浮点 (vec3)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;                // 对应着色器 location = 1
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    // 属性 2: 纹理坐标, 2 个 32 位浮点 (vec2)
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;                // 对应着色器 location = 2
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    return attributeDescriptions;
}

// ====================================================================
// 四、VulkanTutorialApp 类 —— 单例与程序入口
// ====================================================================

VulkanTutorialApp& VulkanTutorialApp::getInstance() {
    // C++11 起, 函数内静态局部变量的初始化由编译器保证线程安全,
    // 第一次调用 getInstance() 时才会构造对象 (懒加载)。
    // 静态对象在程序生命周期内只有一份, 因此整个程序最多只有一个
    // VulkanTutorialApp 实例; 程序退出时由运行时自动析构。
    static VulkanTutorialApp instance;
    return instance;
}

VulkanTutorialApp::~VulkanTutorialApp() = default;

void VulkanTutorialApp::run() {
    initWindow();    // 1. 创建 GLFW 窗口
    initVulkan();    // 2. 初始化整个 Vulkan 环境
    initImGui();     // 3. 初始化 Vulkan + GLFW 的 ImGui 后端
    initMenu();      // 4. 创建对象系统菜单与演示子窗口
    mainLoop();      // 5. 渲染主循环
    cleanup();       // 6. 释放所有资源
}

// ====================================================================
// 五、窗口与主流程
// ====================================================================

void VulkanTutorialApp::initWindow() {
    // 初始化 GLFW 库 (必须在任何其他 GLFW 调用之前)
    glfwInit();

    // GLFW_CLIENT_API 设为 GLFW_NO_API:
    // 告诉 GLFW 不要创建 OpenGL/OpenGL ES 上下文 (我们用 Vulkan)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // 允许用户拉伸窗口 (用于演示交换链重建)
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // 主窗口现在只承载菜单, 不再显示旋转立方体。
    window = glfwCreateWindow(WIDTH, HEIGHT, "MathStudio - 工程管理", nullptr, nullptr);
    if (!window)
        throw std::runtime_error("创建窗口失败!");

    applyAppIcon(window);
    applyDarkWindowFrame(window);

    // 把 this 指针存到窗口的用户数据里,
    // 这样回调函数 (static 函数) 就能找回对象实例
    glfwSetWindowUserPointer(window, this);
    // 注册帧缓冲尺寸变化回调: 窗口拉伸时置 framebufferResized = true,
    // 主循环检测到后重建交换链
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    // F1: 显示/隐藏对象系统菜单 (菜单 / 子窗口是原生 Win32 窗口)
    glfwSetKeyCallback(window, keyCallback);
}

void VulkanTutorialApp::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto app = reinterpret_cast<VulkanTutorialApp*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;  // 标记需要重建交换链
}

void VulkanTutorialApp::mainLoop() {
    auto previousTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        const auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaSeconds = std::chrono::duration<float>(
                                 currentTime - previousTime)
                                 .count();
        previousTime = currentTime;
        // 防止窗口最小化/断点调试后出现超大时间步, 导致粒子瞬移。
        if (deltaSeconds > 0.1f) {
            deltaSeconds = 0.1f;
        }

        // 处理窗口事件 (按键/鼠标/窗口消息), 不处理窗口会"假死"。
        // 原生 Win32 菜单 / 子窗口的消息也会由这里统一分发。
        glfwPollEvents();
        // 主窗口只承载菜单: 更新子窗口对象并渲染子窗口。
        if (menu_) {
            menu_->update(deltaSeconds);
            menu_->render();
        }

        // 构建 ImGui 菜单帧
        beginImGuiFrame();

        // 绘制主窗口 (ImGui 菜单 + 交换链呈现)
        drawFrame();
    }
    // 等待 GPU 完成所有已提交的工作, 之后才能安全销毁资源
    vkDeviceWaitIdle(device);
}

void VulkanTutorialApp::keyCallback(GLFWwindow* window, int key, int,
                                    int action, int mods) {
    auto* app = static_cast<VulkanTutorialApp*>(
        glfwGetWindowUserPointer(window));
    if (app == nullptr || action != GLFW_PRESS) {
        return;
    }

    if (key == GLFW_KEY_F1) {
        app->toggleMenu();
        return;
    }

    // F5 也保存工程, 便于在子窗口/测试中快速落盘。
    if (key == GLFW_KEY_F5 && app->menu_) {
        std::string path = app->menu_->projectPath();
        if (path.empty()) {
            path = "projects/" + app->menu_->projectName() + ".math";
        }
        std::string error;
        app->menu_->saveProject(path, error);
        return;
    }

    // Ctrl+S: 保存工程; 没有路径时保存到 projects/<工程名>.math。
    if (app->menu_ &&
        shortcutMatches(appSettings().saveShortcut, key, mods)) {
        std::string path = app->menu_->projectPath();
        if (path.empty()) {
            path = "projects/" + app->menu_->projectName() + ".math";
        }

        std::string error;
        app->menu_->saveProject(path, error);
        return;
    }

    // Ctrl+O: 重新打开当前工程路径 (方便验证/恢复)。
    if (app->menu_ &&
        shortcutMatches(appSettings().openShortcut, key, mods) &&
        !app->menu_->projectPath().empty()) {
        std::string error;
        app->menu_->openProject(app->menu_->projectPath(), error);
    }
}

void VulkanTutorialApp::toggleMenu() {
    if (!menu_) {
        return;
    }
    if (menu_->isVisible()) {
        menu_->hide();
    } else {
        try {
            menu_->show();
        } catch (...) {
            // GLFW 回调是 C 函数指针, 不能让异常穿过回调边界。
        }
    }
}

void VulkanTutorialApp::initImGui() {
    // ================================================================
    // 1. 创建 ImGui 专用描述符池
    //
    // ImGui 会把字体图集上传成一张纹理, 渲染文字时需要:
    //   * 一个 COMBINED_IMAGE_SAMPLER 描述符 (binding = 0);
    //   * 一个 descriptor set 来持有它。
    //
    // 这里只需要 1 个 set / 1 个 sampler, 因为菜单只使用字体图集。
    // ================================================================
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // FREE_DESCRIPTOR_SET_BIT 允许 ImGui 后端在重建字体纹理时释放旧 set。
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr,
                               &imguiDescriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("创建 ImGui 描述符池失败!");
    }

    // ================================================================
    // 2. 创建 ImGui 上下文
    //
    // 一个进程通常只创建一个全局上下文。
    // 窗口/输入/渲染后端都会把状态挂到这个上下文上。
    // ================================================================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // 允许用 Tab/方向键/回车操作菜单。
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 不生成 imgui.ini, 避免在运行目录留下布局文件。
    io.IniFilename = nullptr;

    // ================================================================
    // 3. 视觉样式
    //
    // 统一主题: 近黑灰色窗口、橙色按钮、白色文字。
    // 这些圆角/颜色只影响 ImGui 控件的外观, 不影响 Vulkan 管线。
    // ================================================================
    std::error_code settingsError;
    const std::filesystem::path settingsPath = defaultSettingsPath();
    if (std::filesystem::exists(settingsPath, settingsError) &&
        !settingsError) {
        std::string error;
        loadSettingsFile(settingsPath.u8string(), error);
    }

    applyMathStudioTheme();
    appliedThemeRevision_ = uiSettingsRevision();

    // ================================================================
    // 4. 加载中文字体
    //
    // ImGui 默认字体只有 ASCII; 菜单里有中文, 所以从 Windows 字体目录
    // 加载黑体, 并请求完整的 CJK 字符范围。
    //
    // 如果字体文件不存在, 退回 ImGui 内置字体 (中文会显示为 '?')。
    // ================================================================
    ImFont* font = nullptr;
    const char* fontPath = "C:\\Windows\\Fonts\\simhei.ttf";

    std::error_code error;
    if (std::filesystem::exists(fontPath, error)) {
        font = io.Fonts->AddFontFromFileTTF(
            fontPath,
            18.0f,
            nullptr,
            mathStudioGlyphRanges());
    }

    if (font == nullptr) {
        io.Fonts->AddFontDefault();
    }

    // ================================================================
    // 5. 初始化 GLFW 平台后端
    //
    // 第二个参数 true 表示让 ImGui 安装 GLFW 输入回调。
    // 它会链式调用我们之前安装的回调 (F1 等)。
    // ================================================================
    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        throw std::runtime_error("初始化 ImGui GLFW 后端失败!");
    }

    // ================================================================
    // 6. 初始化 Vulkan 渲染后端
    //
    // 必须在主窗口的 renderPass 创建之后调用, 因为 ImGui 的图形管线
    // 需要与 renderPass 的附件格式/子通道兼容。
    // ================================================================
    initImGuiVulkanBackend();

    // ================================================================
    // 7. 上传字体图集
    //
    // CreateFontsTexture 会把 io.Fonts 里的像素上传到 VkImage,
    // 并创建对应的 sampler/descriptor set。
    // ================================================================
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        throw std::runtime_error("创建 ImGui 字体纹理失败!");
    }

    imguiInitialized_ = true;
}

void VulkanTutorialApp::initImGuiVulkanBackend() {
    // ImGui_ImplVulkan_InitInfo 必须零初始化,
    // 未显式设置的字段 (Allocator / CheckVkResultFn 等) 保持默认。
    ImGui_ImplVulkan_InitInfo info{};

    // ImGui 后端只会使用 Vulkan 1.0 的核心命令。
    info.ApiVersion = VK_API_VERSION_1_0;

    // 复用主程序已经创建的实例/物理设备/逻辑设备。
    info.Instance = instance;
    info.PhysicalDevice = physicalDevice;
    info.Device = device;

    // ImGui 的绘制命令提交到图形队列。
    info.QueueFamily = graphicsFamilyIndex;
    info.Queue = graphicsQueue;

    // 字体纹理使用 initImGui() 创建的描述符池。
    info.DescriptorPool = imguiDescriptorPool_;

    // 主窗口的 renderPass: 颜色附件 + 深度附件。
    // ImGui 只写入颜色附件, 不进行深度测试。
    info.RenderPass = renderPass;
    info.Subpass = 0;

    // 交换链至少 2 张图像; ImGui 内部按这个数量管理帧资源。
    info.MinImageCount = std::max(
        2u, static_cast<uint32_t>(swapChainImages.size()));
    info.ImageCount = info.MinImageCount;

    // 没有开启 MSAA, 使用 1 个采样点。
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // 不使用管线缓存。
    info.PipelineCache = VK_NULL_HANDLE;

    if (!ImGui_ImplVulkan_Init(&info)) {
        throw std::runtime_error("初始化 ImGui Vulkan 后端失败!");
    }
}

void VulkanTutorialApp::shutdownImGuiVulkanBackend() {
    // 交换链重建时, 旧 renderPass 会被销毁;
    // ImGui 的管线绑定在旧 renderPass 上, 必须先关闭后端。
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplVulkan_Shutdown();
    }
}

void VulkanTutorialApp::shutdownImGui() {
    if (!imguiInitialized_) {
        return;
    }

    // 顺序很重要:
    //   1. Vulkan 后端 (依赖 device/renderPass)
    //   2. GLFW 后端 (依赖 window)
    //   3. ImGui 上下文
    //   4. 描述符池 (依赖 device)
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (imguiDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = VK_NULL_HANDLE;
    }

    imguiInitialized_ = false;
}

void VulkanTutorialApp::beginImGuiFrame() {
    if (!imguiInitialized_) {
        return;
    }

    // ImGui 每帧的顺序是固定的:
    //   1. Vulkan 后端 NewFrame (准备顶点/索引缓冲)
    //   2. GLFW 后端 NewFrame (采集鼠标/键盘)
    //   3. ImGui::NewFrame (开始记录控件)
    //   4. 构建 UI
    //   5. ImGui::Render (生成绘制数据)
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (appliedThemeRevision_ != uiSettingsRevision()) {
        applyMathStudioTheme();
        appliedThemeRevision_ = uiSettingsRevision();
    }

    if (menu_) {
        menu_->drawImGui();
    }

    ImGui::Render();
}

// ====================================================================
// 主菜单初始化
//
// 启动时不再创建或生成任何示例工程。
// 所有 .math 工程都由用户通过“打开工程”手动导入。
// ====================================================================

void VulkanTutorialApp::initMenu() {
    if (menu_) {
        return;
    }

    // 先构造子窗口共享的 Vulkan 资源视图。
    viewportContext_ = std::make_shared<ViewportContext>();
    viewportContext_->instance = instance;
    viewportContext_->physicalDevice = physicalDevice;
    viewportContext_->device = device;
    viewportContext_->graphicsQueue = graphicsQueue;
    viewportContext_->presentQueue = presentQueue;
    viewportContext_->graphicsFamily = graphicsFamilyIndex;
    viewportContext_->presentFamily = presentFamilyIndex;
    viewportContext_->commandPool = commandPool;
    viewportContext_->vertexBuffer = vertexBuffer;
    viewportContext_->indexBuffer = indexBuffer;
    viewportContext_->depthFormat = findDepthFormat();

    menu_ = std::make_unique<Menu>("MathStudio");
    menu_->setViewportContext(viewportContext_);
    menu_->setProjectName("工程");

    // 启动时没有任何工程窗口, 主菜单显示历史工程列表。
    // 用户通过“打开工程”手动选择 .math 文件。
    menu_->clearSubWindows();
    menu_->setProjectName("工程");
    menu_->setProjectPath("");
    menu_->show();

    // 在加载任何工程之前扫描动态插件 DLL。
    // 工程中的插件类型必须能在出厂前/启动时注册到工厂。
    std::string dynamicPluginError;
    DynamicPluginManager::instance().discoverDefault(dynamicPluginError);

    // 可选: 启动时直接打开指定工程 (便于从资源管理器/命令行打开 .math)。
    if (const char* openProject = std::getenv("MATH_OPEN_PROJECT")) {
        if (openProject[0] != '\0') {
            std::string error;
            if (!menu_->openProject(openProject, error)) {
                std::cerr << "打开工程失败: " << error << '\n';
            }
        }
    }
}

// ====================================================================
// 六、Vulkan 初始化总编排
// ====================================================================

void VulkanTutorialApp::initVulkan() {
    createInstance();             // 教程 03-01: Vulkan 实例
    setupDebugMessenger();        // 教程 03-02: 验证层 + 调试回调
    createSurface();              // 教程 04-00: 窗口表面
    pickPhysicalDevice();         // 教程 03-03: 挑选物理设备
    createLogicalDevice();        // 教程 03-04: 创建逻辑设备与队列
    createSwapChain();            // 教程 04-01: 交换链
    createImageViews();           // 教程 04-02: 图像视图
    createRenderPass();           // 教程 05-03: 渲染通道
    createDescriptorSetLayout();  // 教程 05-00: 描述符集布局
    createGraphicsPipeline();     // 教程 05-01/02: 着色器模块 + 图形管线
    createCommandPool();          // 教程 06-01: 命令池
    createDepthResources();       // 教程 07: 深度缓冲
    createFramebuffers();         // 教程 06-00: 帧缓冲
    createTextureImage();         // 教程 06-00: 纹理图像 (程序化棋盘格)
    createTextureImageView();     // 教程 06-01: 纹理图像视图
    createTextureSampler();       // 教程 06-01: 纹理采样器
    createVertexBuffer();         // 教程 04-01/02: 顶点缓冲 + 暂存缓冲
    createIndexBuffer();          // 教程 04-03: 索引缓冲
    createUniformBuffers();       // 教程 05-00: 统一缓冲
    createDescriptorPool();       // 教程 05-01: 描述符池
    createDescriptorSets();       // 教程 05-01: 描述符集
    createCommandBuffers();       // 教程 06-01: 命令缓冲 (每图像一个)
    createSyncObjects();          // 教程 06-02/03: 信号量 + 围栏
}

// ====================================================================
// 七、实例与验证层 (教程 03-01 / 03-02)
// ====================================================================

void VulkanTutorialApp::createInstance() {
    // 先确认所需的验证层在系统上可用
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("请求的验证层不可用! 请安装 Vulkan SDK");
    }

    // ---- VkApplicationInfo: 应用信息 (可选, 但建议填写) ----
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Tutorial";  // 应用名称
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);  // 应用版本
    appInfo.pEngineName = "No Engine";             // 引擎名称
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);  // 引擎版本
    appInfo.apiVersion = VK_API_VERSION_1_0;       // 使用的 Vulkan API 版本 (1.0 兼容性最广)

    // ---- VkInstanceCreateInfo: 实例创建信息 ----
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // 获取实例所需的扩展 (窗口表面扩展由 GLFW 提供)
    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // 启用验证层
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        // 关键技巧: 把调试回调信息挂到实例创建信息的 pNext 链上,
        // 这样连 vkCreateInstance 自身的问题也能被捕获 (实例销毁前回调一直有效)
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // macOS 需要启用可移植性子集 (Windows/Linux 上该代码被编译掉)
#if defined(__APPLE__)
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    // 真正创建实例
    // 参数: 创建信息 / 自定义内存分配器 (nullptr = 默认) / 输出实例句柄
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("创建 Vulkan 实例失败!");
    }
}

bool VulkanTutorialApp::checkValidationLayerSupport() const {
    // 第一步: 枚举系统上所有可用的层
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    // 第二步: 检查每个所需层是否在可用列表中
    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound)
            return false;  // 有层缺失
    }
    return true;
}

std::vector<const char*> VulkanTutorialApp::getRequiredExtensions() const {
    // GLFW 查询它自己需要的实例扩展 (内部对应 VK_KHR_surface + 平台表面扩展)
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        // 调试工具扩展: 提供 vkCreateDebugUtilsMessengerEXT 等调试回调 API
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
#if defined(__APPLE__)
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
    return extensions;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanTutorialApp::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*) {
    // 只打印警告及以上级别的消息 (VERBOSE/INFO 太吵)
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[验证层] " << pCallbackData->pMessage << std::endl << std::endl;
    }
    // 返回 VK_FALSE 表示"继续执行" (返回 VK_TRUE 会中止导致问题的调用)
    return VK_FALSE;
}

void VulkanTutorialApp::populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT& createInfo) const {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    // 关注哪些严重程度的消息 (这里全部关注)
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    // 关注哪些类型的消息 (常规/验证/性能)
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;  // 回调函数指针
    createInfo.pUserData = nullptr;              // 传给回调的自定义数据 (不需要)
}

void VulkanTutorialApp::setupDebugMessenger() {
    if (!enableValidationLayers)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("创建调试信使失败!");
    }
}

VkResult VulkanTutorialApp::CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) const {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    return VK_ERROR_EXTENSION_NOT_PRESENT;  // 扩展不存在
}

void VulkanTutorialApp::DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator) const {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func != nullptr)
        func(instance, debugMessenger, pAllocator);
}

// ====================================================================
// 八、窗口表面 (教程 04-00)
// ====================================================================

void VulkanTutorialApp::createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("创建窗口表面失败!");
    }
}

// ====================================================================
// 九、物理设备与逻辑设备 (教程 03-03 / 03-04)
// ====================================================================

void VulkanTutorialApp::pickPhysicalDevice() {
    // 第一步: 枚举所有支持 Vulkan 的物理设备
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("找不到支持 Vulkan 的 GPU!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // 第二步: 给每个设备打分, 选出最高分
    // 使用 multimap 自动按分数排序 (分数相同的设备也会保留)
    std::multimap<int, VkPhysicalDevice> candidates;
    for (const auto& device : devices) {
        int score = rateDeviceSuitability(device);
        candidates.insert(std::make_pair(score, device));
    }

    // 检查最高分设备的分数是否 > 0 (0 = 不满足基本需求)
    if (candidates.rbegin()->first > 0) {
        physicalDevice = candidates.rbegin()->second;
    } else {
        throw std::runtime_error("找不到满足需求的 GPU!");
    }
}

int VulkanTutorialApp::rateDeviceSuitability(VkPhysicalDevice device) const {
    // ---- 硬性检查 1: 设备属性 ----
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    // ---- 硬性检查 2: 设备特性 ----
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    int score = 0;

    // 独立显卡 (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) 性能最好, 加分最多
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }
    // 最大纹理尺寸是性能的粗略指标
    score += deviceProperties.limits.maxImageDimension2D;

    // ---- 硬性检查 3: 必须能找到图形队列族和呈现队列族 ----
    QueueFamilyIndices indices = QueueFamilyIndices::findQueueFamilies(device, surface);
    if (!indices.isComplete())
        return 0;

    // ---- 硬性检查 4: 必须支持所需的设备扩展 (交换链) ----
    if (!checkDeviceExtensionSupport(device))
        return 0;

    // ---- 硬性检查 5: 交换链参数必须可用 ----
    SwapChainSupportDetails swapChainSupport =
        SwapChainSupportDetails::querySwapChainSupport(device, surface);
    if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty())
        return 0;

    // ---- 硬性检查 6: 必须支持各向异性过滤 (纹理采样器要用) ----
    if (!deviceFeatures.samplerAnisotropy)
        return 0;

    return score;
}

bool VulkanTutorialApp::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
    // 第一步: 枚举设备支持的所有扩展
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    // 第二步: 把所需扩展放入集合, 逐个"划掉"可用的,
    // 最后集合为空 = 全部满足
    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
#if defined(__APPLE__)
    requiredExtensions.insert("VK_KHR_portability_subset");  // macOS (MoltenVK) 必需
#endif
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

void VulkanTutorialApp::createLogicalDevice() {
    // 找到物理设备上的队列族索引
    QueueFamilyIndices indices = QueueFamilyIndices::findQueueFamilies(physicalDevice, surface);

    // ---- 队列创建信息 ----
    // 图形族和呈现族可能是同一个, 用 std::set 去重
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(), indices.presentFamily.value()};

    // 队列优先级: 0.0~1.0, 影响多个队列之间的命令调度
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;   // 使用哪个队列族
        queueCreateInfo.queueCount = 1;                   // 该族里创建几个队列 (1 个就够)
        queueCreateInfo.pQueuePriorities = &queuePriority;  // 队列优先级数组
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // ---- 设备特性 ----
    // VkPhysicalDeviceFeatures: 要启用的可选特性 (默认全为 VK_FALSE)
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;  // 启用各向异性过滤 (纹理采样器要用)

    // ---- 逻辑设备创建信息 ----
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    // 启用设备扩展 (交换链)
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    // 验证层: 新版本 Vulkan 中实例与设备的层概念已统一,
    // 这里与实例保持一致 (旧驱动需要显式启用)
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // 创建逻辑设备
    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("创建逻辑设备失败!");
    }

    // 保存队列族索引, 供子窗口渲染器创建交换链时使用
    graphicsFamilyIndex = indices.graphicsFamily.value();
    presentFamilyIndex = indices.presentFamily.value();

    // 从逻辑设备中取出队列句柄 (队列随逻辑设备创建, 这里只是获取引用)
    // 参数: 设备 / 队列族 / 队列索引 (0 = 该族第一个) / 输出队列
    vkGetDeviceQueue(device, graphicsFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamilyIndex, 0, &presentQueue);
}

// ====================================================================
// 十、交换链与图像视图 (教程 04-01 / 04-02)
// ====================================================================

void VulkanTutorialApp::createSwapChain() {
    // 查询当前物理设备 + 表面的交换链支持详情
    SwapChainSupportDetails swapChainSupport =
        SwapChainSupportDetails::querySwapChainSupport(physicalDevice, surface);

    // 挑选: 表面格式 / 呈现模式 / 图像尺寸 (三个决策函数见下)
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    // ---- 图像数量 ----
    // 建议比最少数量多一张: 等待驱动内部操作时仍有可用图像, 减少卡顿
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    // 但不能超过最大值 (maxImageCount == 0 表示没有上限)
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    // ---- 交换链创建信息 ----
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;                 // 在哪个表面上呈现
    createInfo.minImageCount = imageCount;        // 图像数量
    createInfo.imageFormat = surfaceFormat.format;      // 图像像素格式
    createInfo.imageColorSpace = surfaceFormat.colorSpace;  // 颜色空间
    createInfo.imageExtent = extent;              // 图像尺寸
    createInfo.imageArrayLayers = 1;              // 图像层数 (立体渲染才需要 > 1)
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // 用途: 颜色附件 (渲染目标)

    // ---- 图像共享模式 ----
    // 若图形族与呈现族不同, 图像要能在两个族之间共享, 必须用并发模式;
    // 相同则用独占模式 (性能更好)
    QueueFamilyIndices indices = QueueFamilyIndices::findQueueFamilies(physicalDevice, surface);
    uint32_t queueFamilyIndicesArr[] = {
        indices.graphicsFamily.value(), indices.presentFamily.value()};
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndicesArr;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;       // 可选 (独占模式下忽略)
        createInfo.pQueueFamilyIndices = nullptr;   // 可选
    }

    // 如果表面被旋转了 (如竖屏手机), 这里指定预变换让图像摆正
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    // 合成透明度: OPAQUE 表示不与其他窗口混合 (绝大多数情况)
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;     // 呈现模式
    createInfo.clipped = VK_TRUE;             // 被遮挡的像素不关心 (可优化)
    // 重建交换链时传入旧交换链 (窗口拉伸后重建, 见 recreateSwapChain)
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("创建交换链失败!");
    }

    // ---- 取回交换链图像 ----
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    // 保存格式与尺寸 (图像视图/渲染通道/视口都要用)
    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

VkSurfaceFormatKHR VulkanTutorialApp::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) const {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;  // 最佳选择
        }
    }
    return availableFormats[0];  // 退回第一个 (多数驱动至少提供一种)
}

VkPresentModeKHR VulkanTutorialApp::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) const {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;  // 规范保证 FIFO 一定可用
}

VkExtent2D VulkanTutorialApp::chooseSwapExtent(
    const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        // 驱动指定了尺寸 (如 Windows 桌面), 直接用
        return capabilities.currentExtent;
    }
    // 自己从窗口获取实际像素尺寸并夹取到合法区间
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)};

    actualExtent.width = std::clamp(actualExtent.width,
        capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}

void VulkanTutorialApp::createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        // 颜色附件: 只访问颜色分量, Mip 级别 0, 层 0
        swapChainImageViews[i] = createImageView(
            swapChainImages[i], swapChainImageFormat,
            VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

// ====================================================================
// 十一、渲染通道 (教程 05-03)
// ====================================================================

void VulkanTutorialApp::createRenderPass() {
    // ---- 附件 0: 颜色附件 ----
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;    // 格式与交换链图像一致
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;  // 1 倍采样 (多重采样章节未包含)
    // loadOp: 渲染前对附件做什么
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;    // 清屏 (用 clearValue 的颜色)
    // storeOp: 渲染后对附件做什么
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // 保留结果 (要呈现到屏幕)
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // 不用模板缓冲
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 初始布局: 渲染开始前图像的布局 (UNDEFINED = 不关心之前内容)
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // 最终布局: 渲染结束后图像的布局 (准备呈现到屏幕)
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // ---- 附件 1: 深度附件 (教程 07) ----
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();       // 深度格式 (如 D32_SFLOAT)
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 渲染前清空深度 (1.0 = 最远)
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // 渲染后不需要保留
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // 最终布局: 深度附件布局 (供深度测试使用)
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // ---- 附件引用: 子通道里以什么布局使用这些附件 ----
    // 颜色附件引用
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;  // 指向附件数组的第 0 个 (颜色附件)
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // 渲染期间的最佳布局

    // 深度附件引用
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;  // 指向附件数组的第 1 个 (深度附件)
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // ---- 子通道定义 ----
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  // 图形管线
    subpass.colorAttachmentCount = 1;              // 1 个颜色附件
    subpass.pColorAttachments = &colorAttachmentRef;  // 颜色附件引用数组
    // 深度附件 (片段着色器里通过 layout(location = 0) 的输出写入颜色附件)
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // ---- 子通道依赖 ----
    // 声明两个"隐式过渡": 外部 -> 子通道 0 (加载时), 子通道 0 -> 外部 (存储时)
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;   // 渲染通道开始之前
    dependency.dstSubpass = 0;                     // 子通道 0
    // 源阶段: 等待颜色附件输出阶段 + 早期片段测试阶段完成
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // 目标阶段: 颜色附件输出阶段 + 早期片段测试阶段
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // 目标访问: 需要写入颜色附件 + 深度附件 (对应 loadOp = CLEAR)
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // ---- 渲染通道创建信息 ----
    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("创建渲染通道失败!");
    }
}

// ====================================================================
// 十二、描述符集布局 (教程 05-00)
// ====================================================================

void VulkanTutorialApp::createDescriptorSetLayout() {
    // ---- binding 0: UBO (MVP 矩阵) ----
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;  // 对应着色器 layout(binding = 0)
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;  // 统一缓冲类型
    uboLayoutBinding.descriptorCount = 1;  // 1 个缓冲 (数组的话可以更多)
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;  // 顶点着色器使用
    uboLayoutBinding.pImmutableSamplers = nullptr;  // 与采样器无关

    // ---- binding 1: 组合图像采样器 (纹理) ----
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;  // 对应着色器 layout(binding = 1)
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;  // 图像 + 采样器打包
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;  // 片元着色器使用
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
        uboLayoutBinding, samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("创建描述符集布局失败!");
    }
}

// ====================================================================
// 十三、图形管线 (教程 05-01 / 05-02)
// ====================================================================

void VulkanTutorialApp::createGraphicsPipeline() {
    // ---- 1. 读取着色器字节码 (SPIR-V 由 CMake 用 glslc 编译生成) ----
    auto vertShaderCode = loadShaderCode("vert.spv");  // 顶点着色器
    auto fragShaderCode = loadShaderCode("frag.spv");  // 片元着色器

    // ---- 2. 创建着色器模块 ----
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    // ---- 3. 着色器阶段信息 ----
    // 顶点着色器阶段
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;  // 顶点阶段
    vertShaderStageInfo.module = vertShaderModule;           // 哪个模块
    vertShaderStageInfo.pName = "main";  // 入口函数名 (SPIR-V 中 main 是惯例)
    // pSpecializationInfo: 着色器常量特化 (可优化, 本示例不用)

    // 片元着色器阶段
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;  // 片元阶段
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo, fragShaderStageInfo};

    // ---- 4. 顶点输入状态 ----
    // 描述顶点数据的排布 (绑定描述 + 属性描述, 见 Vertex 结构体的静态函数)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    auto bindingDescription = Vertex::getBindingDescription();      // 步长/速率
    auto attributeDescriptions = Vertex::getAttributeDescriptions();  // 属性列表
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // ---- 5. 图元装配状态 ----
    // 顶点如何组成图元: 每 3 个顶点组成一个三角形
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // 三角形列表
    inputAssembly.primitiveRestartEnable = VK_FALSE;  // 不启用图元重启 (特殊索引 0xFFFF 不生效)

    // ---- 6. 视口与裁剪状态 ----
    // 视口: 把裁剪空间坐标映射到帧缓冲像素坐标;
    // 裁剪矩形: 只有矩形内的片元会被保留。
    // 这里声明为"动态状态" (见下方 dynamicStates),
    // 实际值在命令缓冲里用 vkCmdSetViewport/vkCmdSetScissor 设置。
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;   // 1 个视口 (动态提供)
    viewportState.scissorCount = 1;    // 1 个裁剪矩形 (动态提供)

    // ---- 7. 光栅化状态 ----
    // 把几何图元转换成片元 (像素), 并决定剔除/填充方式
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;  // 不启用深度钳制 (超出近远平面的片元直接丢弃)
    rasterizer.rasterizerDiscardEnable = VK_FALSE;  // 不丢弃所有图元 (必须正常光栅化)
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;  // 三角形填充 (LINE = 线框, POINT = 点)
    rasterizer.lineWidth = 1.0f;             // 线宽 (线框模式用, 宽线需要特性)
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // 剔除背面 (性能优化, 立方体是封闭的)
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // 逆时针为正面 (与立方体顶点绕序一致)
    rasterizer.depthBiasEnable = VK_FALSE;    // 深度偏移 (阴影贴图才需要)

    // ---- 8. 多重采样状态 ----
    // 抗锯齿 (MSAA) 配置。本示例 samples = 1 (无多重采样),
    // 对应教程的"多重采样"章节未包含。
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;  // 不启用采样着色
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;  // 每像素 1 个采样
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // ---- 9. 深度/模板测试状态 (教程 07) ----
    // 深度测试: 只有深度值比已存储的更近的片元才能通过 (解决遮挡)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;    // 启用深度测试
    depthStencil.depthWriteEnable = VK_TRUE;   // 通过测试的片元写入新深度值
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;  // 新深度 < 旧深度才通过 (越近越小)
    depthStencil.depthBoundsTestEnable = VK_FALSE;  // 不使用深度范围测试
    depthStencil.minDepthBounds = 0.0f;  // 深度范围下限 (bounds 关闭时忽略)
    depthStencil.maxDepthBounds = 1.0f;  // 深度范围上限
    depthStencil.stencilTestEnable = VK_FALSE;  // 不启用模板测试
    depthStencil.front = {};  // 正面模板操作 (关闭时忽略)
    depthStencil.back = {};   // 背面模板操作

    // ---- 10. 颜色混合状态 ----
    // 片元着色器输出如何与帧缓冲已有颜色混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;  // RGBA 全部写入
    colorBlendAttachment.blendEnable = VK_FALSE;  // 不混合 (直接覆盖, 本示例不需要透明混合)
    // 以下混合因子在 blendEnable = FALSE 时被忽略 (alpha 混合的经典配置, 供参考)
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;  // 不使用逻辑运算混合 (老式位运算混合)
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;   // 1 个颜色附件
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;  // 混合常量 (blendFactor 为 CONSTANT 时用)
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // ---- 11. 动态状态 ----
    // 视口和裁剪矩形设为动态: 交换链重建 (尺寸变化) 时无需重建管线
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // ---- 12. 管线布局 ----
    // 声明管线用到的描述符集布局 (这里只有 1 个) 和推送常量
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;      // 不使用推送常量
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("创建管线布局失败!");
    }

    // ---- 13. 汇总创建图形管线 ----
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;                          // 2 个着色器阶段
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;    // 顶点输入
    pipelineInfo.pInputAssemblyState = &inputAssembly;    // 图元装配
    pipelineInfo.pViewportState = &viewportState;         // 视口
    pipelineInfo.pRasterizationState = &rasterizer;       // 光栅化
    pipelineInfo.pMultisampleState = &multisampling;      // 多重采样
    pipelineInfo.pDepthStencilState = &depthStencil;      // 深度测试
    pipelineInfo.pColorBlendState = &colorBlending;       // 颜色混合
    pipelineInfo.pDynamicState = &dynamicState;           // 动态状态
    pipelineInfo.layout = pipelineLayout;                 // 管线布局
    pipelineInfo.renderPass = renderPass;                 // 属于哪个渲染通道
    pipelineInfo.subpass = 0;                             // 用于子通道 0
    // 可以从已有管线"继承" (管线派生优化), 本示例不用
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("创建图形管线失败!");
    }

    // ---- 14. 清理着色器模块 ----
    // 管线创建完成后, 着色器模块的使命就结束了 (字节码已拷入管线), 可以销毁
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

VkShaderModule VulkanTutorialApp::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();  // 字节码大小 (字节)
    // code 是 std::vector<char>, 而 API 要求 uint32_t*:
    // SPIR-V 字节码按 32 位字组织, 这里强制转换指针类型
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("创建着色器模块失败!");
    }
    return shaderModule;
}

// ====================================================================
// 十四、帧缓冲与命令池 (教程 06-00 / 06-01)
// ====================================================================

void VulkanTutorialApp::createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        // 附件: 颜色图像视图 (交换链第 i 张图像) + 深度图像视图 (共用一张)
        std::array<VkImageView, 2> attachments = {
            swapChainImageViews[i],
            depthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;  // 与哪个渲染通道配套
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChainExtent.width;   // 帧缓冲宽度
        framebufferInfo.height = swapChainExtent.height; // 帧缓冲高度
        framebufferInfo.layers = 1;                      // 层数

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("创建帧缓冲失败!");
        }
    }
}

void VulkanTutorialApp::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices =
        QueueFamilyIndices::findQueueFamilies(physicalDevice, surface);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  // 允许 vkResetCommandBuffer
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();  // 图形队列族

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("创建命令池失败!");
    }
}

// ====================================================================
// 十五、深度缓冲 (教程 07)
// ====================================================================

void VulkanTutorialApp::createDepthResources() {
    // 挑选合适的深度格式 (见 findDepthFormat 注释)
    VkFormat depthFormat = findDepthFormat();

    // 创建深度图像 (不经过暂存缓冲: 初始内容会被 CLEAR, 无需上传数据)
    createImage(swapChainExtent.width, swapChainExtent.height, 1,
                depthFormat,                          // 深度格式
                VK_IMAGE_TILING_OPTIMAL,              // 最佳平铺 (GPU 高效访问)
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,  // 用途: 深度附件
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,  // 设备本地显存 (GPU 专用)
                depthImage, depthImageMemory);

    // 创建深度图像视图 (只访问深度分量)
    depthImageView = createImageView(depthImage, depthFormat,
                                     VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    // 把深度图像布局从 UNDEFINED 转换到深度附件最佳布局
    transitionImageLayout(depthImage, depthFormat,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
}

VkFormat VulkanTutorialApp::findSupportedFormat(
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        // 查询该格式在此物理设备上的支持特性
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        // 根据平铺方式检查对应的特性集合
        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                   (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("找不到支持所需特性的格式!");
}

VkFormat VulkanTutorialApp::findDepthFormat() const {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool VulkanTutorialApp::hasStencilComponent(VkFormat format) const {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT;
}

// ====================================================================
// 十六、纹理图像 / 视图 / 采样器 (教程 06 全部 + 09 Mipmap)
// ====================================================================

void VulkanTutorialApp::createTextureImage() {
    // ---- 1. 程序化生成棋盘格纹理 (8×8 棋盘, 512×512 像素) ----
    const int texWidth = 512;
    const int texHeight = 512;
    const int checkerSize = 64;  // 每个格子的边长 (512/64 = 8 格)
    // Mipmap 级数: 每级尺寸减半, 直到 1×1 (log2(512)+1 = 10 级)
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    // RGBA8: 每个像素 4 字节
    std::vector<uint8_t> pixels(static_cast<size_t>(texWidth) * texHeight * 4);
    for (int y = 0; y < texHeight; y++) {
        for (int x = 0; x < texWidth; x++) {
            // 棋盘格: 根据所在格子判断颜色 (暖橙 / 深蓝)
            bool even = ((x / checkerSize) + (y / checkerSize)) % 2 == 0;
            uint8_t r = even ? 246 : 52;
            uint8_t g = even ? 135 : 62;
            uint8_t b = even ? 62 : 135;
            size_t offset = (static_cast<size_t>(y) * texWidth + x) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = 255;  // alpha = 不透明
        }
    }
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    // ---- 2. 创建暂存缓冲并填入像素数据 ----
    // 暂存缓冲: CPU 可写的普通内存 (HOST_VISIBLE),
    // 因为设备本地显存 (DEVICE_LOCAL) 通常 CPU 不能直接访问
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,            // 用途: 传输源
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |        // CPU 可见
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,    // 写后自动对 GPU 可见 (免刷缓存)
                 stagingBuffer, stagingBufferMemory);

    // 把像素数据拷贝进暂存缓冲
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);  // 映射到 CPU 地址空间
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);  // 拷贝完成, 解除映射

    // ---- 3. 创建设备本地纹理图像 ----
    // 用途: 传输源 (Mipmap blit 时作为源) + 传输目标 (接收拷贝) + 采样 (着色器读取)
    createImage(texWidth, texHeight, mipLevels,
                VK_FORMAT_R8G8B8A8_SRGB,             // sRGB 格式 (颜色空间感知)
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // 设备本地显存
                textureImage, textureImageMemory);

    // ---- 4. 布局转换: UNDEFINED -> 传输目标 ----
    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);

    // ---- 5. 暂存缓冲 -> 纹理图像 (Mip 级别 0) ----
    copyBufferToImage(stagingBuffer, textureImage,
                      static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

    // ---- 6. 暂存缓冲使命完成, 销毁 ----
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // ---- 7. 生成 Mipmap (教程 09) ----
    // 逐级把上一级缩小一半, 完成所有 Mip 级别
    // 注意: 此函数结束后整个纹理已处于 SHADER_READ_ONLY_OPTIMAL 布局
    generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                    texWidth, texHeight, mipLevels);
}

void VulkanTutorialApp::generateMipmaps(VkImage image, VkFormat imageFormat,
                                        int32_t texWidth, int32_t texHeight,
                                        uint32_t mipLevels) const {
    // 安全检查: 确认该格式支持线性过滤的 blit (大多数显卡都支持)
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("纹理图像格式不支持线性过滤 blit!");
    }

    // 单次提交的命令缓冲 (整个 Mipmap 生成过程一次完成)
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // 内存屏障: 每一级 blit 前后都要做布局转换和同步
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;  // 不跨队列族转移
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;  // 每次只影响 1 级 (逐级转换)

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        // ---- 第 i-1 级: 传输目标 -> 传输源 (准备被 blit 读取) ----
        // 说明: 第 0 级刚接收完暂存缓冲的拷贝 (布局为 TRANSFER_DST),
        //       第 i-1 级 (i>1) 刚作为上一轮 blit 的目标 (也是 TRANSFER_DST),
        //       所以这里的旧布局统一是 TRANSFER_DST_OPTIMAL
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;   // 等待该级写入完成
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;    // 之后传输要读
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,  // 源阶段: 传输写入
                             VK_PIPELINE_STAGE_TRANSFER_BIT,  // 目标阶段: 传输读取
                             0,
                             0, nullptr, 0, nullptr,
                             1, &barrier);

        // ---- blit: 第 i-1 级 -> 第 i 级 (尺寸减半, 线性过滤) ----
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};                          // 源区域起点
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};           // 源区域终点
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;                    // 源 Mip 级别
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};                          // 目标区域起点
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,   // 目标区域终点 (减半)
                              mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;                        // 目标 Mip 级别
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
                       image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,   // 源图像 + 布局
                       image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,   // 目标图像 + 布局 (同一张图!)
                       1, &blit,
                       VK_FILTER_LINEAR);  // 线性过滤 (缩小需要插值)

        // ---- 第 i-1 级: 传输源 -> 着色器只读 (blit 完成, 交还着色器使用) ----
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;      // 等待传输读完
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;        // 之后着色器要读
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0, nullptr, 0, nullptr,
                             1, &barrier);

        // 尺寸减半, 处理下一级
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // ---- 最后一级: 传输目标 -> 着色器只读 ----
    // (它从未作为 blit 源, 只需一次布局转换)
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;   // 等待写入完成
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;      // 之后着色器要读
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0, nullptr, 0, nullptr,
                         1, &barrier);

    // 提交并等待完成
    endSingleTimeCommands(commandBuffer);
}

void VulkanTutorialApp::createTextureImageView() {
    textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                                       VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
}

void VulkanTutorialApp::createTextureSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;  // 放大过滤: 线性 (纹理被拉近时平滑)
    samplerInfo.minFilter = VK_FILTER_LINEAR;  // 缩小过滤: 线性 (纹理被拉远时平滑)
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // U 方向平铺重复
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // V 方向平铺重复
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // W 方向平铺重复
    // 各向异性过滤: 减少倾斜视角下的模糊 (x4 效果已很好)
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 4.0f;  // 设备支持的最大值可查 limits.maxSamplerAnisotropy
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;  // 越界色 (CLAMP 模式用)
    samplerInfo.unnormalizedCoordinates = VK_FALSE;  // 使用归一化坐标 (0~1)
    samplerInfo.compareEnable = VK_FALSE;            // 不做深度比较 (阴影贴图才用)
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    // Mipmap 配置
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  // Mip 级之间线性过渡
    samplerInfo.mipLodBias = 0.0f;   // Mip 级别偏移 (0 = 自动选择)
    samplerInfo.minLod = 0.0f;       // 最低可用 Mip 级别
    samplerInfo.maxLod = static_cast<float>(mipLevels);  // 最高可用 Mip 级别 (全部使用)

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("创建纹理采样器失败!");
    }
}

// ====================================================================
// 十七、图像创建 / 布局转换 / 拷贝 通用工具
// ====================================================================

void VulkanTutorialApp::createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                                    VkFormat format, VkImageTiling tiling,
                                    VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                                    VkImage& image, VkDeviceMemory& imageMemory) const {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;      // 2D 图像
    imageInfo.extent.width = width;              // 宽度
    imageInfo.extent.height = height;            // 高度
    imageInfo.extent.depth = 1;                  // 深度 (2D 图像固定为 1)
    imageInfo.mipLevels = mipLevels;             // Mip 级别数
    imageInfo.arrayLayers = 1;                   // 数组层数 (立方体贴图为 6)
    imageInfo.format = format;                   // 像素格式
    imageInfo.tiling = tiling;                   // 平铺方式
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // 初始布局 (内容将被覆盖)
    imageInfo.usage = usage;                     // 用途
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;   // 采样数 (无多重采样)
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // 独占 (仅图形队列族使用)
    imageInfo.flags = 0;  // 稀疏图像等特殊标志 (不用)

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("创建图像失败!");
    }

    // ---- 分配显存 ----
    // 查询图像需要多少内存、可以用哪些内存类型
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;  // 需要的内存大小
    // 挑选内存类型: 满足内存类型位 + 属性要求 (如设备本地)
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("分配图像内存失败!");
    }

    // 把内存绑定到图像 (偏移 0)
    vkBindImageMemory(device, image, imageMemory, 0);
}

void VulkanTutorialApp::transitionImageLayout(VkImage image, VkFormat format,
                                              VkImageLayout oldLayout, VkImageLayout newLayout,
                                              uint32_t mipLevels) const {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;   // 转换前布局
    barrier.newLayout = newLayout;   // 转换后布局
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;  // 不跨队列族
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;           // 作用的目标图像
    // 作用的子资源范围: Mip 级别 0 ~ mipLevels-1, 层 0
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // 深度格式的转换要注意模板分量 (本示例只用深度)
    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(format))
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    // ---- 根据布局转换对, 确定访问掩码和管线阶段 ----
    VkPipelineStageFlags sourceStage;       // 源阶段: 转换前谁在访问
    VkPipelineStageFlags destinationStage;  // 目标阶段: 转换后谁要访问

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // 未初始化 -> 传输目标 (接收拷贝数据)
        barrier.srcAccessMask = 0;  // 之前无人访问
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;  // 传输要写入
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;       // 最早阶段
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;     // 传输阶段
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // 传输目标 -> 着色器只读 (数据拷贝完毕, 交给着色器采样)
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;  // 等待传输写完
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;     // 着色器要读
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        // 未初始化 -> 深度附件 (深度缓冲的初始转换)
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;  // 深度测试要读写
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;  // 早期片段测试
    } else {
        throw std::invalid_argument("不支持的图像布局转换!");
    }
    // 注: Mipmap 逐级生成过程中的布局转换
    // (传输目标->传输源 / 传输源->着色器只读) 需要逐级区分 Mip 级别,
    // 在 generateMipmaps 里用内联内存屏障完成, 不走本函数。

    // 提交屏障 (0 个缓冲屏障, 1 个图像屏障)
    vkCmdPipelineBarrier(commandBuffer,
                         sourceStage, destinationStage,
                         0,
                         0, nullptr, 0, nullptr,
                         1, &barrier);

    endSingleTimeCommands(commandBuffer);
}

void VulkanTutorialApp::copyBufferToImage(VkBuffer buffer, VkImage image,
                                          uint32_t width, uint32_t height) const {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // 拷贝区域: 指定源缓冲的哪个范围拷到图像的哪个子资源
    VkBufferImageCopy region{};
    region.bufferOffset = 0;       // 源缓冲起始偏移
    region.bufferRowLength = 0;    // 行间距 (0 = 紧密排列)
    region.bufferImageHeight = 0;  // 图像高度间距 (0 = 紧密排列)
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;            // 目标 Mip 级别
    region.imageSubresource.baseArrayLayer = 0;      // 目标层
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};                  // 目标图像区域起点
    region.imageExtent = {width, height, 1};         // 目标图像区域大小

    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  // 目标图像当前布局
                           1, &region);

    endSingleTimeCommands(commandBuffer);
}

VkImageView VulkanTutorialApp::createImageView(VkImage image, VkFormat format,
                                               VkImageAspectFlags aspectFlags,
                                               uint32_t mipLevels) const {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;                       // 目标图像
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;    // 2D 视图
    viewInfo.format = format;                     // 格式 (与图像一致)
    // 分量映射: 默认恒等映射 (r->r, g->g, ...), 也可用它做颜色通道交换
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    // 子资源范围: 访问哪些分量 / Mip 级别 / 层
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("创建图像视图失败!");
    }
    return imageView;
}

// ====================================================================
// 十八、顶点缓冲 / 索引缓冲 (教程 04 全部)
// ====================================================================

void VulkanTutorialApp::createVertexBuffer() {
    // ---- 立方体顶点数据 ----
    // 24 个顶点: 6 个面 × 4 个角, 每个面一种颜色, 纹理坐标 0~1
    // 注意: 每个面的绕序为逆时针 (从面外侧看), 与管线的 frontFace 设置一致
    const std::vector<Vertex> vertices = {
        // 前面 (z = +0.5)
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // 后面 (z = -0.5)
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // 左面 (x = -0.5)
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        // 右面 (x = +0.5)
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        // 底面 (y = -0.5)
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        // 顶面 (y = +0.5)
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
    };

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // ---- 暂存缓冲 (CPU 可写) ----
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,          // 传输源
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |      // CPU 可见
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,  // 写后自动可见
                 stagingBuffer, stagingBufferMemory);

    // 拷贝顶点数据到暂存缓冲
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // ---- 设备本地目标缓冲 (GPU 专用, 访问速度最快) ----
    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |          // 传输目标
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,     // 顶点缓冲
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,       // 设备本地显存
                 vertexBuffer, vertexBufferMemory);

    // 暂存缓冲 -> 设备本地缓冲 (GPU 内部拷贝)
    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    // 暂存缓冲使命完成, 销毁
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void VulkanTutorialApp::createIndexBuffer() {
    // 36 个索引: 12 个三角形 × 3 个索引, 数字对应上面的顶点数组。
    // 关键: 每个面都必须按"从立方体外侧看逆时针"排列,
    // 才能与管线中的 frontFace = COUNTER_CLOCKWISE + 背面剔除一致。
    // 旧版本的后/右/顶面绕序相反, 旋转到前方时会被当成背面剔除,
    // 看起来就像"透明/不显示"。
    const std::vector<uint32_t> indices = {
        0,  1,  2,  2,  3,  0,    // 前面 (+Z)
        4,  6,  5,  6,  4,  7,    // 后面 (-Z)
        8,  9,  10, 10, 11, 8,    // 左面 (-X)
        12, 14, 13, 14, 12, 15,   // 右面 (+X)
        16, 17, 18, 18, 19, 16,   // 底面 (-Y)
        20, 22, 21, 22, 20, 23    // 顶面 (+Y)
    };

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    // ---- 暂存缓冲 (同顶点缓冲的套路) ----
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // ---- 设备本地索引缓冲 ----
    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,  // 索引缓冲
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 indexBuffer, indexBufferMemory);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void VulkanTutorialApp::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags properties,
                                     VkBuffer& buffer, VkDeviceMemory& bufferMemory) const {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;                       // 缓冲大小
    bufferInfo.usage = usage;                     // 用途
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // 独占 (仅图形队列族)

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("创建缓冲失败!");
    }

    // ---- 分配内存 ----
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("分配缓冲内存失败!");
    }

    // 把内存绑定到缓冲
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void VulkanTutorialApp::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer,
                                   VkDeviceSize size) const {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // 拷贝区域: 源偏移 0 -> 目标偏移 0, 大小 size
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

uint32_t VulkanTutorialApp::findMemoryType(uint32_t typeFilter,
                                           VkMemoryPropertyFlags properties) const {
    // 查询物理设备的内存类型列表
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // 条件: 类型在允许位掩码中 && 属性完全满足要求
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("找不到符合要求的内存类型!");
}

// ====================================================================
// 十九、单次命令辅助 (用于一次性上传/转换操作)
// ====================================================================

VkCommandBuffer VulkanTutorialApp::beginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;  // 主命令缓冲
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;  // 只提交一次

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanTutorialApp::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
    vkEndCommandBuffer(commandBuffer);

    // 提交到图形队列 (没有信号量/围栏, 直接等待完成)
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    // 等待该队列上所有命令完成 (简单直接, 适合初始化阶段的一次性操作)
    vkQueueWaitIdle(graphicsQueue);

    // 释放临时命令缓冲
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

// ====================================================================
// 二十、统一缓冲与描述符 (教程 05 全部)
// ====================================================================

void VulkanTutorialApp::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(swapChainImages.size());
    uniformBuffersMemory.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        createBuffer(bufferSize,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,     // 统一缓冲
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |   // CPU 可见
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,  // 自动一致
                     uniformBuffers[i], uniformBuffersMemory[i]);
    }
}

void VulkanTutorialApp::updateUniformBuffer(uint32_t currentImage) {
    // 用时钟计算程序运行了多久 (秒), 驱动旋转动画
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(
                     currentTime - startTime)
                     .count();

    UniformBufferObject ubo{};
    // 模型矩阵: 绕 Z 轴每 4 秒转一圈 (90°/秒)
    ubo.model = glm::rotate(glm::mat4(1.0f),
                            time * glm::radians(90.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    // 视图矩阵: 摄像机从 (2,2,2) 看向原点 (立方体), 上方向为 Z 轴
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    // 投影矩阵: 45° 视场角, 近平面 0.1, 远平面 10
    ubo.proj = glm::perspective(glm::radians(45.0f),
                                swapChainExtent.width / static_cast<float>(swapChainExtent.height),
                                0.1f, 10.0f);
    // 关键: Vulkan 裁剪空间 Y 轴向下 (与 OpenGL 相反),
    // 翻转投影矩阵的 Y 分量修正这个差异
    ubo.proj[1][1] *= -1;

    // 把 UBO 写入 GPU 内存: 映射 -> 拷贝 -> 解除映射
    void* data;
    vkMapMemory(device, uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uniformBuffersMemory[currentImage]);
}

void VulkanTutorialApp::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;       // UBO 类型
    poolSizes[0].descriptorCount = static_cast<uint32_t>(swapChainImages.size());
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;  // 采样器类型
    poolSizes[1].descriptorCount = static_cast<uint32_t>(swapChainImages.size());

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(swapChainImages.size());  // 最多分配的描述符集数
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // 允许单独释放

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("创建描述符池失败!");
    }
}

void VulkanTutorialApp::createDescriptorSets() {
    // 每个交换链图像一个描述符集, 布局都相同 (descriptorSetLayout)
    std::vector<VkDescriptorSetLayout> layouts(swapChainImages.size(), descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;               // 从哪个池分配
    allocInfo.descriptorSetCount = static_cast<uint32_t>(swapChainImages.size());
    allocInfo.pSetLayouts = layouts.data();                  // 每个集的布局

    descriptorSets.resize(swapChainImages.size());
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("分配描述符集失败!");
    }

    // 为每个描述符集填写绑定
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        // ---- binding 0 的数据: UBO 缓冲 ----
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];  // 该图像的 UBO
        bufferInfo.offset = 0;                  // 从缓冲开头
        bufferInfo.range = sizeof(UniformBufferObject);  // 整个 UBO 大小

        // ---- binding 1 的数据: 图像 + 采样器 ----
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // 着色器只读布局
        imageInfo.imageView = textureImageView;  // 纹理图像视图
        imageInfo.sampler = textureSampler;      // 纹理采样器

        // ---- 两次写入 ----
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];       // 写入哪个集
        descriptorWrites[0].dstBinding = 0;                   // 绑定号 0 (UBO)
        descriptorWrites[0].dstArrayElement = 0;              // 数组起始元素
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;              // 写入 1 个描述符
        descriptorWrites[0].pBufferInfo = &bufferInfo;        // UBO 数据

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;                   // 绑定号 1 (采样器)
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;          // 图像+采样器数据

        vkUpdateDescriptorSets(device,
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(),
                               0, nullptr);  // 0 个"拷贝式"更新
    }
}

// ====================================================================
// 二十一、命令缓冲与渲染 (教程 06 全部)
// ====================================================================

void VulkanTutorialApp::createCommandBuffers() {
    commandBuffers.resize(swapChainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;                 // 从图形命令池分配
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;   // 主命令缓冲 (可直接提交)
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("分配命令缓冲失败!");
    }
}

void VulkanTutorialApp::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    // ---- 1. 开始记录 ----
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("开始记录命令缓冲失败!");
    }

    // ---- 2. 开始主窗口渲染通道 ----
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.02f, 0.025f, 0.04f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    // ---- 3. 交给 ImGui Vulkan 后端绘制菜单 ----
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("结束记录命令缓冲失败!");
    }
}

// ====================================================================
// 二十二、同步对象与逐帧渲染 (教程 06-02 / 06-03)
// ====================================================================

void VulkanTutorialApp::createSyncObjects() {
    // 获取信号量: 每个飞行帧一个 (索引 currentFrame, 见成员变量注释)
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    // 渲染完成信号量: 每个交换链图像一个 (索引 imageIndex)
    renderFinishedSemaphores.resize(swapChainImages.size());
    // 围栏: 每个飞行帧一个
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // 关键: 围栏初始为"已触发"状态!
    // 因为第一帧 drawFrame 会先 vkWaitForFences,
    // 若围栏从未被提交过 (未触发), CPU 将永远等待 (死锁)
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("创建获取信号量失败!");
        }
    }
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("创建渲染完成信号量失败!");
        }
    }
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("创建围栏失败!");
        }
    }
}

void VulkanTutorialApp::drawFrame() {
    // ---- 1. 等待当前帧的围栏 ----
    // UINT64_MAX = 无限等待 (也支持设置超时)
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // ---- 2. 获取交换链图像 ----
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        device, swapChain, UINT64_MAX,                      // 无限等待
        imageAvailableSemaphores[currentFrame],             // 拿到图像后触发此信号量 (按帧索引)
        VK_NULL_HANDLE,                                     // 不用围栏
        &imageIndex);                                       // 输出: 图像索引

    // 交换链与窗口尺寸不匹配 (如窗口被拉伸) -> 重建后重来
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        // SUBOPTIMAL 表示还能用但不是最优 (例如窗口尺寸变了但驱动容忍),
        // 这种情况继续渲染, 呈现时再处理
        throw std::runtime_error("获取交换链图像失败!");
    }

    // ---- 3. 等待该图像上可能存在的旧围栏 ----
    // 每个交换链图像同时只能被一帧使用; 若这张图还被上一帧占着, 先等它完成
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    // 登记: 这张图像现在由当前帧的围栏"占用"
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];

    // ---- 4. 主窗口现在只渲染 ImGui 菜单, 不需要更新 3D UBO ----

    // ---- 5. 重置围栏与命令缓冲, 重新记录 ----
    // 围栏重置为未触发 (提交命令时 GPU 会再次触发它)
    vkResetFences(device, 1, &inFlightFences[currentFrame]);
    // 清空旧命令, 重新记录 (因为每帧的 UBO 内容不同)
    vkResetCommandBuffer(commandBuffers[imageIndex], 0);
    recordCommandBuffer(commandBuffers[imageIndex], imageIndex);

    // ---- 6. 提交命令到图形队列 ----
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // 等待: 图像可用信号量 (拿到图像后才开始渲染)
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    // 在哪个管线阶段等待 (颜色附件输出阶段: 开始写颜色前要等图像就绪)
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

    // 完成渲染后触发: 渲染完成信号量 (按图像索引, 见成员变量注释)
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // 提交 + 围栏: 该帧所有命令执行完时触发 inFlightFences[currentFrame]
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("提交绘制命令失败!");
    }

    // ---- 7. 呈现到屏幕 ----
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    // 等待: 渲染完成信号量 (渲染完才能呈现)
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;  // 呈现哪张图像

    presentInfo.pResults = nullptr;  // 每个交换链的结果 (单交换链时可为空)

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    // ---- 8. 处理交换链过期 ----
    // 三种情况需要重建交换链: 过期 / 次优 / 用户拉伸了窗口
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("呈现交换链图像失败!");
    }

    // ---- 9. 推进到下一帧 ----
    // 围栏/信号量数组只有 MAX_FRAMES_IN_FLIGHT 份, 循环复用
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ====================================================================
// 二十三、交换链重建 (教程 03-04 交换链重建)
// ====================================================================

void VulkanTutorialApp::recreateSwapChain() {
    // ---- 处理窗口最小化 ----
    // 最小化时帧缓冲尺寸为 0, 交换链无法创建;
    // 循环等待 (glfwWaitEvents 阻塞等事件, 不占 CPU) 直到窗口恢复
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    // ---- 等待 GPU 完成所有工作 ----
    // 销毁交换链相关资源前必须确保 GPU 不再使用它们
    vkDeviceWaitIdle(device);

    // ImGui 的管线绑定在旧 renderPass 上, 先关闭后端;
    // 新 renderPass 创建后再重新初始化。
    if (imguiInitialized_) {
        shutdownImGuiVulkanBackend();
    }

    // ---- 销毁旧的交换链相关资源 ----
    cleanupSwapChain();

    // ---- 按新尺寸全部重建 ----
    // 与 initVulkan 中的顺序一致 (注意: 不需要重建的
    // 实例/设备/渲染通道等在这里没有出现)
    createSwapChain();
    createImageViews();
    createRenderPass();             // 渲染通道含格式引用, 保险起见重建
    createGraphicsPipeline();       // 管线含视口等状态引用
    createDepthResources();         // 深度缓冲尺寸随窗口变化
    createFramebuffers();
    createUniformBuffers();         // UBO 数量 = 图像数量
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    // 图像数量可能变化, 重新初始化"图像占用"记录
    imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);

    if (imguiInitialized_) {
        initImGuiVulkanBackend();
        if (!ImGui_ImplVulkan_CreateFontsTexture()) {
            throw std::runtime_error("重建 ImGui 字体纹理失败!");
        }
    }
}

void VulkanTutorialApp::cleanupSwapChain() {
    // 深度资源 (尺寸随窗口变化)
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);

    // 帧缓冲
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    // 命令缓冲 (池还在, 直接释放缓冲)
    vkFreeCommandBuffers(device, commandPool,
                         static_cast<uint32_t>(commandBuffers.size()),
                         commandBuffers.data());

    // 图形管线与管线布局
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    // 图像视图与交换链
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device, swapChain, nullptr);

    // 统一缓冲 (UBO 数量随图像数量变化)
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        vkDestroyBuffer(device, uniformBuffers[i], nullptr);
        vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
    }

    // 描述符池 (释放池会连带释放其中所有描述符集)
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
}

// ====================================================================
// 二十四、全局清理
// ====================================================================

void VulkanTutorialApp::cleanup() {
    // 对象系统 (菜单 + 子窗口) 持有自己的 Vulkan 交换链/资源,
    // 必须在销毁共享设备 / 命令池 / 几何缓冲之前销毁。
    menu_.reset();
    viewportContext_.reset();

    // ImGui 后端使用 renderPass / device, 必须在销毁交换链前关闭
    shutdownImGui();

    // 依赖交换链的资源
    cleanupSwapChain();

    // 纹理相关 (不随交换链重建)
    vkDestroySampler(device, textureSampler, nullptr);
    vkDestroyImageView(device, textureImageView, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory, nullptr);

    // 顶点/索引缓冲
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);

    // 描述符集布局
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    // 同步对象 (注意三种对象的数量不同: 见 createSyncObjects)
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }
    for (size_t i = 0; i < renderFinishedSemaphores.size(); i++) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    }

    // 命令池
    vkDestroyCommandPool(device, commandPool, nullptr);

    // 逻辑设备
    vkDestroyDevice(device, nullptr);

    // 调试信使与表面、实例
    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    // GLFW 窗口与库
    glfwDestroyWindow(window);
    glfwTerminate();
}


