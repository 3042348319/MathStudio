// ====================================================================
// Vulkan 完整示例 —— 窗口与应用程序类声明 (window.h)
// 参照教程: https://docs.vulkan.net.cn/tutorial/latest/00_Introduction.html
//
// 本文件包含:
//   * 全局常量 (窗口尺寸 / 飞行帧数 / 验证层 / 设备扩展)
//   * 辅助数据结构 (队列族索引 / 交换链支持详情 / 顶点 / UBO)
//   * VulkanTutorialApp 类的完整声明 (所有成员函数的功能说明)
//
// 本示例完整覆盖了教程的以下章节:
//   * 03 绘制三角形: 实例 / 验证层 / 物理设备 / 逻辑设备 / 窗口表面 /
//     交换链 / 图像视图 / 图形管线 / 渲染通道 / 帧缓冲 / 命令缓冲 /
//     渲染与演示 / 飞行中的帧 / 交换链重建
//   * 04 顶点缓冲: 顶点输入描述 / 顶点缓冲区 / 暂存缓冲区 / 索引缓冲区
//   * 05 统一缓冲: 描述符集布局 / 描述符池 / 描述符集 (MVP 矩阵旋转动画)
//   * 06 纹理映射: 图像 / 图像视图与采样器 / 组合图像采样器 (程序化棋盘格纹理)
//   * 07 深度缓冲: 深度图像 + 深度测试 (立方体各面正确遮挡)
//   * 09 生成 Mipmap: vkCmdBlitImage 逐级生成 Mipmap (远距离不闪烁)
//
// 运行效果: 一个贴着棋盘格纹理的彩色立方体绕 Z 轴旋转,
//           窗口可任意拉伸, 交换链会自动重建。
//
// 设计说明:
//   VulkanTutorialApp 采用单例模式 (Singleton):
//     - 构造函数私有, 外部无法直接创建对象;
//     - 拷贝构造 / 拷贝赋值 / 移动构造 / 移动赋值全部删除,
//       无法复制出第二个实例;
//     - 只能通过 VulkanTutorialApp::getInstance() 获取全局唯一实例,
//       整个程序中最多只会存在一个 VulkanTutorialApp 对象。
// ====================================================================
#pragma once

// ====================================================================
// 一、头文件与全局常量
// ====================================================================

// VK_USE_PLATFORM_WIN32_KHR: 启用 Windows 平台相关的 Vulkan 扩展声明
// (必须在包含 glfw3.h 之前定义, GLFW 需要用它创建 Win32 窗口表面)
#define VK_USE_PLATFORM_WIN32_KHR
// GLFW_INCLUDE_VULKAN: 让 GLFW 自动包含 <vulkan/vulkan.h>,
// 这样就不用手动 include Vulkan 头文件, 且顺序一定正确
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// GLM 数学库
// GLM_FORCE_RADIANS: 强制所有角度参数使用弧度 (旧版 GLM 默认是角度)
#define GLM_FORCE_RADIANS
// GLM_FORCE_DEPTH_ZERO_TO_ONE: 使用 Vulkan 的深度范围 [0, 1]
// (OpenGL 是 [-1, 1], 该宏让 GLM 生成 Vulkan 风格的投影矩阵)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>      // std::array (顶点属性描述数组)
#include <cstdint>    // uint32_t / uint64_t
#include <memory>     // std::unique_ptr (菜单 / 子窗口)
#include <optional>   // std::optional (队列族索引可能不存在)
#include <string>     // std::string (着色器文件名)
#include <vector>     // std::vector

// ---- 窗口尺寸 ----
constexpr uint32_t WIDTH = 800;   // 窗口初始宽度 (像素)
constexpr uint32_t HEIGHT = 600;  // 窗口初始高度 (像素)

// ---- 飞行中的帧数 ----
// 表示同时在"飞行中"(已提交给 GPU 但未完成)的帧的最大数量。
// 设为 2: 当第 0 帧还在渲染时, CPU 可以提前准备第 1 帧, 提高吞吐。
// 注意: 帧数越多延迟越高, 2 是延迟与性能的良好折中。
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// ---- 验证层列表 ----
// VK_LAYER_KHRONOS_validation: Khronos 官方验证层,
// 会检查 API 参数合法性、资源生命周期、同步问题等, 是开发期的"安全带"。
// 验证层只在调试构建中启用 (见下方 enableValidationLayers)。
inline const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// ---- 设备扩展列表 ----
// VK_KHR_SWAPCHAIN_EXTENSION_NAME (即 VK_KHR_swapchain):
// 交换链扩展, 把渲染结果呈现到屏幕所必需。
inline const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// ---- 是否启用验证层 ----
// NDEBUG 宏在 Release 构建时由编译器定义,
// 因此: Debug 构建启用验证层, Release 构建不启用 (提升性能)。
#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

// ====================================================================
// 二、着色器读取
// ====================================================================
// 说明: 读取 SPIR-V 的函数已移到 shader_loader.h / shader_loader.cpp,
// 供主窗口和子窗口渲染器共用。

// ====================================================================
// 三、自定义数据结构定义
// ====================================================================

/**
 * 队列族索引集合
 * Vulkan 的"队列族"(Queue Family)是一组功能相同的队列。
 * 每个队列族有一个索引号, 我们用该结构体记录:
 *  - 哪个队列族支持图形命令 (graphicsFamily)
 *  - 哪个队列族支持向窗口表面呈现 (presentFamily)
 * 两者可能是同一个族, 也可能是不同的族。
 * 使用 std::optional 因为某些设备可能没有支持呈现的队列族
 * (虽然对大多数显卡而言一定存在)。
 */
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;  // 支持图形命令的队列族索引
    std::optional<uint32_t> presentFamily;   // 支持表面呈现的队列族索引

    /**
     * 判断所需的队列族是否全部找到
     * @return true 表示图形队列族与呈现队列族都已找到
     */
    bool isComplete() const;

    /**
     * 查询指定物理设备上的队列族信息
     * @param device 物理设备句柄
     * @param surface 窗口表面句柄 (用于检查呈现支持)
     * @return 记录图形 / 呈现队列族索引的结构体
     */
    static QueueFamilyIndices findQueueFamilies(
        VkPhysicalDevice device, VkSurfaceKHR surface);
};

/**
 * 交换链支持详情
 * 创建交换链前需要查询物理设备 + 表面组合支持哪些参数:
 *  - capabilities : 表面能力 (最小/最大图像数, 当前宽高等)
 *  - formats      : 支持的表面颜色格式列表
 *  - presentModes : 支持的呈现模式列表 (立即/垂直同步/邮箱等)
 * 这些参数是根据“物理设备+Surface”的组合来选取的
 */
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;        // 表面能力
    std::vector<VkSurfaceFormatKHR> formats;     // 支持的表面格式
    std::vector<VkPresentModeKHR> presentModes;  // 支持的呈现模式

    /**
     * 查询交换链支持详情 (三个函数调用分别获取能力/格式/呈现模式)
     * @param device 物理设备句柄
     * @param surface 窗口表面句柄
     * @return 填充好的交换链支持详情
     */
    static SwapChainSupportDetails querySwapChainSupport(
        VkPhysicalDevice device, VkSurfaceKHR surface);
};

/**
 * 顶点结构体
 * 每个顶点包含:
 *  - pos      : 3D 位置
 *  - color    : 颜色 (与纹理相乘, 让每个面有不同的色调)
 *  - texCoord : 纹理坐标 (UV)
 * 内存布局与着色器中的输入属性一一对应 (见 getAttributeDescriptions)。
 */
struct Vertex {
    glm::vec3 pos;       // 顶点位置 (location = 0)
    glm::vec3 color;     // 顶点颜色 (location = 1)
    glm::vec2 texCoord;  // 纹理坐标 (location = 2)

    /**
     * 获取顶点输入绑定描述
     * 告诉 Vulkan: 顶点数据以多大的步长 (stride) 在缓冲区中排列、
     * 是逐顶点读取 (VK_VERTEX_INPUT_RATE_VERTEX) 还是逐实例读取。
     * @return VkVertexInputBindingDescription 绑定描述
     */
    static VkVertexInputBindingDescription getBindingDescription();

    /**
     * 获取顶点属性描述数组
     * 告诉 Vulkan: 每个顶点属性的 location、数据类型、在顶点内的偏移。
     * location 必须与顶点着色器中 layout(location = N) 一致。
     * @return 3 个属性的描述 (位置 / 颜色 / 纹理坐标)
     */
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
};

/**
 * 统一缓冲对象 (Uniform Buffer Object, UBO)
 * 在着色器之间共享的全局数据: MVP 三个矩阵。
 * 内存布局必须与着色器中 std140 布局的 UniformBufferObject 一致:
 * std140 规则下 mat4 按 4 个 vec4 (64 字节) 对齐,
 * 因此 C++ 端用 alignas(16) 保证与 glm::mat4 的对齐一致。
 */
struct UniformBufferObject {
    alignas(16) glm::mat4 model;  // 模型矩阵: 模型空间 -> 世界空间
    alignas(16) glm::mat4 view;   // 视图矩阵: 世界空间 -> 摄像机空间
    alignas(16) glm::mat4 proj;   // 投影矩阵: 摄像机空间 -> 裁剪空间
};

// ====================================================================
// 四、VulkanTutorialApp 类 —— 封装整个 Vulkan 应用程序 (单例)
// ====================================================================

class Menu;  // 菜单窗口 (对象系统 UI, 定义见 menu.h)
struct ViewportContext;  // 子窗口共享的 Vulkan 资源 (定义见 viewport_context.h)

/**
 * VulkanTutorialApp 类
 * 负责从零搭建整个 Vulkan 渲染环境并持续渲染一帧帧画面。
 * 初始化流程 (initVulkan) 严格遵循教程章节顺序:
 * 实例 -> 验证层 -> 表面 -> 物理设备 -> 逻辑设备 -> 交换链 -> ...
 *
 * 单例模式说明:
 *  - 构造函数 / 析构函数私有, 外部不能直接创建或销毁对象;
 *  - 拷贝与移动操作全部 delete, 不能产生第二个实例;
 *  - 唯一获取方式是静态函数 getInstance()。
 */
class VulkanTutorialApp {
public:
    /**
     * 获取全局唯一的 VulkanTutorialApp 实例
     * 使用 C++11 起线程安全的"函数内静态局部变量"实现懒加载:
     * 第一次调用时构造对象, 之后调用都返回同一个对象,
     * 程序退出时由运行时自动析构。
     * 这是本类唯一的创建入口 (构造函数是私有的)。
     * @return 全局唯一实例的引用
     */
    static VulkanTutorialApp& getInstance();

    // 程序总入口: 初始化窗口 -> 初始化 Vulkan -> 主循环 -> 清理
    // 注意: 单例已经由 getInstance() 创建, 这里只负责运行与释放资源。
    void run();

    // ---- 单例约束: 禁止拷贝 / 移动, 防止产生第二个实例 ----
    VulkanTutorialApp(const VulkanTutorialApp&) = delete;             // 禁止拷贝构造
    VulkanTutorialApp& operator=(const VulkanTutorialApp&) = delete;  // 禁止拷贝赋值
    VulkanTutorialApp(VulkanTutorialApp&&) = delete;                  // 禁止移动构造
    VulkanTutorialApp& operator=(VulkanTutorialApp&&) = delete;       // 禁止移动赋值

private:
    // ---- 单例约束: 构造 / 析构私有, 只能由 getInstance() 内部创建 ----
    VulkanTutorialApp() = default;   // 构造函数私有
    ~VulkanTutorialApp();            // 析构函数私有 (定义在 window.cpp, 以便管理菜单)

    // ==================== 窗口相关成员 ====================
    GLFWwindow* window = nullptr;             // GLFW 窗口句柄
    bool framebufferResized = false;          // 帧缓冲尺寸是否被用户改变 (窗口拉伸时置 true)

    // ==================== Vulkan 核心句柄 ====================
    VkInstance instance = VK_NULL_HANDLE;                    // Vulkan 实例 (全局入口, 连接应用与驱动)
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE; // 调试回调信使 (接收验证层消息)
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;        // 物理设备 (显卡本身)
    VkDevice device = VK_NULL_HANDLE;                        // 逻辑设备 (与显卡交互的接口)
    VkQueue graphicsQueue = VK_NULL_HANDLE;                  // 图形队列 (提交绘制命令)
    VkQueue presentQueue = VK_NULL_HANDLE;                   // 呈现队列 (把图像送到屏幕)
    VkSurfaceKHR surface = VK_NULL_HANDLE;                   // 窗口表面 (连接 Vulkan 与操作系统窗口)

    // ==================== 交换链相关 ====================
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;       // 交换链 (一组待显示的图像, 轮流呈现)
    std::vector<VkImage> swapChainImages;            // 交换链中的图像句柄
    VkFormat swapChainImageFormat = VK_FORMAT_UNDEFINED;  // 交换链图像像素格式
    VkExtent2D swapChainExtent{0, 0};                // 交换链图像尺寸 (像素)
    std::vector<VkImageView> swapChainImageViews;    // 每个交换链图像的"图像视图"
    // 帧缓冲句柄 (createFramebuffers 创建, cleanupSwapChain 销毁)
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // ==================== 渲染管线相关 ====================
    VkRenderPass renderPass = VK_NULL_HANDLE;                    // 渲染通道 (定义渲染的附件与子通道)
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;  // 描述符集布局 (声明着色器需要哪些资源)
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;            // 管线布局 (描述符集 + 推送常量)
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;                // 图形管线 (着色器 + 固定功能状态的总和)
    VkCommandPool commandPool = VK_NULL_HANDLE;                  // 命令池 (命令缓冲的分配器)

    // ==================== 深度缓冲相关 ====================
    VkImage depthImage = VK_NULL_HANDLE;            // 深度图像 (存储每个像素的深度值)
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;  // 深度图像占用的显存
    VkImageView depthImageView = VK_NULL_HANDLE;    // 深度图像视图

    // ==================== 纹理相关 ====================
    VkImage textureImage = VK_NULL_HANDLE;            // 纹理图像 (程序化生成的棋盘格)
    VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;  // 纹理图像占用的显存
    VkImageView textureImageView = VK_NULL_HANDLE;    // 纹理图像视图
    VkSampler textureSampler = VK_NULL_HANDLE;        // 纹理采样器 (定义过滤/寻址/Mipmap 方式)
    uint32_t mipLevels = 1;                           // 纹理 Mipmap 级数

    // ==================== 顶点/索引缓冲相关 ====================
    VkBuffer vertexBuffer = VK_NULL_HANDLE;            // 顶点缓冲区 (GPU 显存中的顶点数据)
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE; // 顶点缓冲区显存
    VkBuffer indexBuffer = VK_NULL_HANDLE;             // 索引缓冲区 (GPU 显存中的索引数据)
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE; // 索引缓冲区显存

    // ==================== 统一缓冲相关 ====================
    std::vector<VkBuffer> uniformBuffers;        // 每个交换链图像一个 UBO (防止帧间数据竞争)
    std::vector<VkDeviceMemory> uniformBuffersMemory;  // UBO 显存

    // ==================== 描述符相关 ====================
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;  // 描述符池 (描述符集的内存来源)
    std::vector<VkDescriptorSet> descriptorSets;       // 描述符集 (把 UBO/纹理实际绑定给着色器)

    // ==================== 命令缓冲相关 ====================
    std::vector<VkCommandBuffer> commandBuffers;  // 每个交换链图像一个命令缓冲 (记录绘制命令)

    // ==================== 同步对象 ====================
    // 两种信号量的分配策略不同, 这是教程之外的一个重要细节:
    //  * imageAvailableSemaphores 按"飞行帧"分配 (索引 currentFrame):
    //    该信号量由 vkAcquireNextImageKHR 触发、由提交的渲染命令消费;
    //    复用同一帧槽位前, 围栏已保证上一帧提交完成 (信号量必然已被消费), 安全。
    //  * renderFinishedSemaphores 按"交换链图像"分配 (索引 imageIndex):
    //    该信号量由渲染命令触发、由呈现引擎消费, 而呈现引擎的消费时机
    //    不受围栏约束! 在 MAILBOX 等模式下图像数可能多于飞行帧数,
    //    按帧复用会出现"呈现引擎还在使用信号量就被再次触发"的错误
    //    (验证层会报告)。只有重新获取同一张图像 (呈现引擎已释放它)
    //    才能保证该图像的信号量空闲, 因此必须按图像分配。
    std::vector<VkSemaphore> imageAvailableSemaphores;  // 信号量: 图像获取完成 (可开始渲染)
    std::vector<VkSemaphore> renderFinishedSemaphores;  // 信号量: 渲染完成 (可开始呈现)
    // 围栏按"飞行帧"分配 (CPU 与 GPU 之间的同步, 每帧一个)
    std::vector<VkFence> inFlightFences;                // 围栏: 该帧是否已完全执行完毕 (CPU 侧查询)
    std::vector<VkFence> imagesInFlight;                // 记录每个交换链图像当前正在使用的围栏

    size_t currentFrame = 0;  // 当前帧索引 (0 ~ MAX_FRAMES_IN_FLIGHT-1 循环)

    // 逻辑设备使用的队列族索引 (子窗口渲染器创建交换链时需要)
    uint32_t graphicsFamilyIndex = 0;
    uint32_t presentFamilyIndex = 0;

    // ==================== 对象系统 / 菜单 ====================
    std::shared_ptr<ViewportContext> viewportContext_;  // 子窗口共享的 Vulkan 资源
    std::unique_ptr<Menu> menu_;  // 菜单 / 对象浏览器 (ImGui + Vulkan)
    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;  // ImGui 字体描述符池
    bool imguiInitialized_ = false;
    unsigned appliedThemeRevision_ = 0;

    // ====================================================================
    // 五、窗口与主流程
    // ====================================================================

    /**
     * 初始化 GLFW 窗口
     * 注意: 本程序不使用 OpenGL, 只借用 GLFW 创建窗口并接收输入事件。
     */
    void initWindow();

    /**
     * GLFW 帧缓冲尺寸变化回调
     * 注意: 这是 static 函数 (GLFW 要求 C 函数指针),
     * 通过 glfwGetWindowUserPointer 取回 app 对象。
     * @param window 触发回调的 GLFW 窗口
     */
    static void framebufferResizeCallback(GLFWwindow* window, int, int);

    /**
     * 渲染主循环
     * 每帧: 处理窗口事件 -> 更新对象 -> 绘制一帧, 直到用户关闭窗口。
     */
    void mainLoop();

    /**
     * 初始化 ImGui (Vulkan + GLFW 后端)
     * 创建描述符池、加载中文字体、初始化两个后端。
     */
    void initImGui();

    /** 关闭 ImGui 并销毁描述符池 (必须在销毁 device 之前调用) */
    void shutdownImGui();

    /** 用当前 renderPass / 交换链信息初始化 ImGui Vulkan 后端 */
    void initImGuiVulkanBackend();

    /** 关闭 ImGui Vulkan 后端 (交换链重建前调用) */
    void shutdownImGuiVulkanBackend();

    /** 开始一帧 ImGui: NewFrame -> 菜单 UI -> Render */
    void beginImGuiFrame();

    /**
     * 初始化对象系统菜单与演示子窗口
     * 在 Vulkan / ImGui 初始化完成后调用:
     *   * 创建 Menu (ImGui 菜单);
     *   * 通过 SubWindow::addObject<Object>() 创建对象;
     *   * 通过 Object::addPlugin<ParticlePlugin>() /
     *     Object::addPlugin<CoordinateSystemPlugin>() 注入行为;
     *   * 通过 Object::addPlugin<ParameterPlugin>() 挂载通用参数;
     *   * 显示菜单并打开第一个子窗口。
     */
    void initMenu();

    /**
     * GLFW 键盘回调
     * 目前只处理 F1: 显示/隐藏对象系统菜单。
     */
    static void keyCallback(GLFWwindow* window, int key, int scancode,
                            int action, int mods);

    /** 显示或隐藏菜单 (F1) */
    void toggleMenu();

    // ====================================================================
    // 六、Vulkan 初始化总编排
    // ====================================================================

    /**
     * 初始化整个 Vulkan 环境
     * 调用顺序与教程章节一一对应, 每个函数的详细说明见各自声明。
     */
    void initVulkan();

    // ====================================================================
    // 七、实例与验证层 (教程 03-01 / 03-02)
    // ====================================================================

    /**
     * 创建 Vulkan 实例 (VkInstance)
     * 实例是 Vulkan 的全局入口: 连接应用程序与显卡驱动,
     * 并声明应用信息、要启用的扩展和验证层。
     */
    void createInstance();

    /**
     * 检查系统是否安装了所需的验证层
     * @return true 表示所有验证层都可用
     */
    bool checkValidationLayerSupport() const;

    /**
     * 获取实例所需的扩展列表
     *  - GLFW 必需扩展: 让 Vulkan 能对接操作系统窗口系统
     *    (Windows 上是 VK_KHR_win32_surface)
     *  - VK_EXT_DEBUG_UTILS_EXTENSION_NAME: 启用验证层的调试回调功能
     * @return 以 const char* 表示的扩展名列表
     */
    std::vector<const char*> getRequiredExtensions() const;

    /**
     * 验证层调试回调函数
     * 每当验证层发现问题 (参数错误/同步问题/性能警告等) 就会调用此函数。
     * 这里简单地打印到标准错误输出, 正式项目可以接日志系统。
     */
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,  // 严重程度 (详细/信息/警告/错误)
        VkDebugUtilsMessageTypeFlagsEXT messageType,            // 消息类型 (常规/验证/性能)
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,  // 消息内容
        void*);                                                 // 用户自定义数据

    /**
     * 填充调试信使创建信息
     * 该结构体被两处使用: 实例创建时的 pNext (捕获早期错误) 和
     * 正式注册调试信使 (setupDebugMessenger)。
     * @param createInfo 待填充的调试信使创建信息
     */
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const;

    /**
     * 注册调试信使 (验证层消息 -> debugCallback)
     * vkCreateDebugUtilsMessengerEXT 是扩展函数, 不在 vulkan-1.dll 的
     * 导出表里, 必须用 vkGetInstanceProcAddr 动态获取函数地址。
     */
    void setupDebugMessenger();

    /**
     * 扩展函数代理: vkCreateDebugUtilsMessengerEXT
     * 通过 vkGetInstanceProcAddr 动态加载扩展函数的地址 (见 setupDebugMessenger 注释)。
     * @return Vulkan 调用结果; 扩展不存在时返回 VK_ERROR_EXTENSION_NOT_PRESENT
     */
    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger) const;

    /**
     * 扩展函数代理: vkDestroyDebugUtilsMessengerEXT (销毁调试信使)
     */
    void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* pAllocator) const;

    // ====================================================================
    // 八、窗口表面 (教程 04-00)
    // ====================================================================

    /**
     * 创建窗口表面 (VkSurfaceKHR)
     * 表面是 Vulkan 与操作系统窗口系统之间的桥梁:
     * Vulkan 渲染出的图像要通过表面才能显示到窗口里。
     * 这里调用 GLFW 的跨平台封装, 实际会调用平台相关的扩展
     * (Windows 上是 vkCreateWin32SurfaceKHR)。
     */
    void createSurface();

    // ====================================================================
    // 九、物理设备与逻辑设备 (教程 03-03 / 03-04)
    // ====================================================================

    /**
     * 挑选物理设备 (显卡)
     * Vulkan 不透明地管理硬件: 先枚举所有物理设备,
     * 按"适合度"打分排序, 选分数最高的一个。
     */
    void pickPhysicalDevice();

    /**
     * 给物理设备打分 (越高越适合)
     * 先检查硬性需求 (队列族/扩展/交换链支持), 不满足直接给 0 分;
     * 满足后再根据设备类型加分。
     * @return 0 表示不可用, 正数越大越优先
     */
    int rateDeviceSuitability(VkPhysicalDevice device) const;

    /**
     * 检查物理设备是否支持所有必需的设备扩展
     * @return true 表示支持全部扩展
     */
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;

    /**
     * 创建逻辑设备 (VkDevice) 并获取队列句柄
     * 逻辑设备是应用与物理设备交互的接口:
     * 声明要使用的队列族、启用的扩展和特性。
     */
    void createLogicalDevice();

    // ====================================================================
    // 十、交换链与图像视图 (教程 04-01 / 04-02)
    // ====================================================================

    /**
     * 创建交换链 (VkSwapchainKHR)
     * 交换链是一组图像队列: 程序渲染到其中的一张图像,
     * 渲染完成后把它"呈现"到屏幕, 同时拿下一张继续渲染 (双缓冲/三缓冲的泛化)。
     */
    void createSwapChain();

    /**
     * 挑选表面格式 (像素格式 + 颜色空间)
     * 优先选择: 8 位 RGBA + sRGB 非线性颜色空间 (标准色彩, 最接近真实观感)
     * 找不到就退回第一个可用格式。
     * @param availableFormats 设备支持的表面格式列表
     * @return 选中的表面格式
     */
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats) const;

    /**
     * 挑选呈现模式
     *  - VK_PRESENT_MODE_FIFO_KHR     : 垂直同步, 队列满则等待 (一定可用)
     *  - VK_PRESENT_MODE_MAILBOX_KHR  : 三重缓冲, 新帧直接覆盖队列 (延迟低且不撕裂)
     * 优先选 MAILBOX, 没有则退回 FIFO。
     * @param availablePresentModes 设备支持的呈现模式列表
     * @return 选中的呈现模式
     */
    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes) const;

    /**
     * 挑选交换链图像尺寸 (交换范围)
     * 特殊值: currentExtent 为 UINT32_MAX 表示驱动允许我们自定义尺寸,
     * 此时把窗口尺寸限制在 [minImageExtent, maxImageExtent] 区间内。
     * @param capabilities 表面能力
     * @return 交换链图像尺寸
     */
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

    /**
     * 为每个交换链图像创建图像视图 (VkImageView)
     * 图像视图描述"如何访问图像": 它是渲染通道/描述符访问图像的入口,
     * 指定了图像格式、访问的 Mip 级别和层范围。
     */
    void createImageViews();

    // ====================================================================
    // 十一、渲染通道 (教程 05-03)
    // ====================================================================

    /**
     * 创建渲染通道 (VkRenderPass)
     * 渲染通道声明了渲染操作的"蓝图":
     *  - 使用哪些附件 (attachment): 颜色附件 + 深度附件
     *  - 每个附件的加载/存储行为
     *  - 子通道 (subpass) 如何引用这些附件
     * 图形管线必须指定自己属于哪个渲染通道。
     */
    void createRenderPass();

    // ====================================================================
    // 十二、描述符集布局 (教程 05-00)
    // ====================================================================

    /**
     * 创建描述符集布局 (VkDescriptorSetLayout)
     * 描述符集布局声明"着色器需要哪些资源、以什么方式访问":
     *  - binding 0: 统一缓冲 (UBO), 顶点着色器使用 —— MVP 矩阵
     *  - binding 1: 组合图像采样器, 片元着色器使用 —— 棋盘格纹理
     * 它只是"模板声明", 真正的资源绑定在 createDescriptorSets 里完成。
     */
    void createDescriptorSetLayout();

    // ====================================================================
    // 十三、图形管线 (教程 05-01 / 05-02)
    // ====================================================================

    /**
     * 创建图形管线 (VkPipeline)
     * 图形管线 = 着色器 + 固定功能状态, 是整个渲染流程的"总装":
     * 着色器阶段 (顶点/片元) -> 顶点输入 -> 图元装配 -> 视口 ->
     * 光栅化 -> 多重采样 -> 深度/模板测试 -> 颜色混合 -> 动态状态。
     */
    void createGraphicsPipeline();

    /**
     * 从 SPIR-V 字节码创建着色器模块 (VkShaderModule)
     * 着色器模块封装一段已编译的着色器字节码, 供管线使用。
     * @param code SPIR-V 字节码 (从 .spv 文件读出)
     * @return 创建好的着色器模块
     */
    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    // ====================================================================
    // 十四、帧缓冲与命令池 (教程 06-00 / 06-01)
    // ====================================================================

    /**
     * 创建帧缓冲 (VkFramebuffer)
     * 帧缓冲 = 渲染通道 + 一组具体附件 (图像视图) 的组合:
     * 渲染时把渲染通道"实例化"到具体的图像上。
     * 每个交换链图像都要有一个对应的帧缓冲。
     */
    void createFramebuffers();

    /**
     * 创建命令池 (VkCommandPool)
     * 命令池是命令缓冲的"分配器", 且与特定队列族绑定:
     * 从该池分配的命令缓冲只能提交到该族的队列。
     * RESET_COMMAND_BUFFER 标志允许单独重置命令缓冲 (每帧重新记录)。
     */
    void createCommandPool();

    // ====================================================================
    // 十五、深度缓冲 (教程 07)
    // ====================================================================

    /**
     * 创建深度资源 (深度图像 + 图像视图)
     * 深度缓冲存储每个像素的深度值, 用于深度测试:
     * 渲染立方体时, 被挡住的背面不会画到前面 (解决遮挡问题)。
     */
    void createDepthResources();

    /**
     * 从候选列表中挑选一个"支持指定特性"的图像格式
     * @param candidates 候选格式列表 (按优先级排列)
     * @param tiling 平铺方式 (LINEAR / OPTIMAL)
     * @param features 需要支持的格式特性 (如深度附件特性)
     * @return 第一个满足要求的格式
     * @throw std::runtime_error 没有候选格式满足要求时抛出
     */
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features) const;

    /**
     * 挑选深度格式
     * 候选: 32 位浮点深度 > 32 位浮点深度+8 位模板 > 24 位深度+8 位模板
     * (越靠前精度越高, 需要的特性: 可作为深度模板附件)
     * @return 选中的深度格式
     */
    VkFormat findDepthFormat() const;

    /**
     * 判断格式是否包含模板分量 (8 位模板与深度打包的格式)
     * 模板分量在布局转换时要设置 VK_IMAGE_ASPECT_STENCIL_BIT。
     * @param format 待检查的图像格式
     * @return true 表示格式带有模板分量
     */
    bool hasStencilComponent(VkFormat format) const;

    // ====================================================================
    // 十六、纹理图像 / 视图 / 采样器 (教程 06 全部 + 09 Mipmap)
    // ====================================================================

    /**
     * 创建纹理图像
     * 流程: 程序化生成棋盘格像素 -> 暂存缓冲 (CPU 可写) ->
     *       布局转换 -> 拷贝到设备本地图像 -> 逐级生成 Mipmap。
     * 纹理数据在教程里来自图片文件, 这里改为代码生成,
     * 避免额外的图片文件依赖, 效果一样。
     */
    void createTextureImage();

    /**
     * 生成 Mipmap (教程 09)
     * Mipmap = 同一纹理的逐级缩小版本 (1/2、1/4、1/8 ...):
     * 远处/倾斜表面采样低级别, 避免摩尔纹和闪烁。
     * 用 vkCmdBlitImage 把 Mip i-1 缩小拷贝到 Mip i (线性过滤)。
     * @param image 目标纹理图像
     * @param imageFormat 图像格式
     * @param texWidth/texHeight 第 0 级纹理尺寸
     * @param mipLevels Mip 级别总数
     */
    void generateMipmaps(VkImage image, VkFormat imageFormat,
                         int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const;

    /**
     * 创建纹理图像视图
     * 与交换链图像视图类似, 但声明了全部 Mip 级别 (着色器采样时需要)。
     */
    void createTextureImageView();

    /**
     * 创建纹理采样器 (VkSampler)
     * 采样器定义"如何从纹理里取值":
     *  - 放大/缩小过滤: 线性 (双线性插值, 平滑过渡)
     *  - Mipmap: 线性过滤, 逐级过渡
     *  - 寻址模式: REPEAT (UV 超出 0~1 时平铺重复, 本示例 UV 在范围内)
     *  - 各向异性过滤: 倾斜表面更清晰 (需要设备特性支持)
     */
    void createTextureSampler();

    // ====================================================================
    // 十七、图像创建 / 布局转换 / 拷贝 通用工具
    // ====================================================================

    /**
     * 创建图像并分配显存 (通用工具)
     * @param width/height 图像尺寸
     * @param mipLevels   Mip 级别数
     * @param format      像素格式
     * @param tiling      平铺方式 (OPTIMAL = GPU 高效布局)
     * @param usage       用途位 (传输/采样/附件等)
     * @param properties  内存属性 (DEVICE_LOCAL = 显卡显存)
     * @param image/imageMemory 输出句柄
     */
    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                     VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory) const;

    /**
     * 转换图像布局 (通用工具)
     * Vulkan 中图像在不同用途下有不同的"布局"(内存排布),
     * 比如渲染目标和纹理采样的布局不同。转换布局通过内存屏障完成,
     * 同时还能同步不同阶段的读写。
     * @param oldLayout/newLayout 转换前后的布局
     * @param mipLevels 影响的 Mip 级别数
     */
    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               uint32_t mipLevels) const;

    /**
     * 把缓冲数据拷贝到图像 (通用工具)
     * @param buffer 源缓冲 (暂存缓冲)
     * @param image  目标图像
     * @param width/height 图像尺寸
     */
    void copyBufferToImage(VkBuffer buffer, VkImage image,
                           uint32_t width, uint32_t height) const;

    /**
     * 创建图像视图 (通用工具)
     * @param image 目标图像
     * @param format 图像格式
     * @param aspectFlags 访问的分量 (颜色 / 深度 / 模板)
     * @param mipLevels 可见的 Mip 级别数
     * @return 图像视图句柄
     */
    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspectFlags,
                                uint32_t mipLevels) const;

    // ====================================================================
    // 十八、顶点缓冲 / 索引缓冲 (教程 04 全部)
    // ====================================================================

    /**
     * 创建顶点缓冲区
     * 流程: 创建设备本地目标缓冲 + 暂存缓冲 -> 拷贝数据 -> 提交传输。
     */
    void createVertexBuffer();

    /**
     * 创建索引缓冲区
     * 索引缓冲复用顶点: 立方体 24 个顶点, 36 个索引 (12 个三角形)。
     * 每个面 2 个三角形, 索引顺序同样遵循逆时针绕序。
     */
    void createIndexBuffer();

    /**
     * 创建缓冲并分配显存 (通用工具)
     * @param size       缓冲大小 (字节)
     * @param usage      用途 (顶点/索引/传输/统一缓冲等)
     * @param properties 内存属性 (HOST_VISIBLE = CPU 可访问, DEVICE_LOCAL = 显存)
     * @param buffer/bufferMemory 输出句柄
     */
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory) const;

    /**
     * 在两个缓冲之间拷贝数据 (GPU 侧, 通过传输队列命令)
     * @param srcBuffer 源缓冲
     * @param dstBuffer 目标缓冲
     * @param size 拷贝字节数
     */
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;

    /**
     * 查找符合要求的内存类型索引
     * @param typeFilter 允许的内存类型位掩码 (来自内存需求查询)
     * @param properties 需要的内存属性 (如 HOST_VISIBLE / DEVICE_LOCAL)
     * @return 内存类型索引
     * @throw std::runtime_error 找不到匹配的内存类型时抛出
     */
    uint32_t findMemoryType(uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) const;

    // ====================================================================
    // 十九、单次命令辅助 (用于一次性上传/转换操作)
    // ====================================================================

    /**
     * 开始一段"单次命令"
     * 分配一个临时命令缓冲, 开启记录, 供一次性操作 (拷贝/布局转换) 使用。
     * @return 已开始记录的命令缓冲
     */
    VkCommandBuffer beginSingleTimeCommands() const;

    /**
     * 结束并提交"单次命令", 等待 GPU 执行完毕
     * @param commandBuffer beginSingleTimeCommands 返回的命令缓冲
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    // ====================================================================
    // 二十、统一缓冲与描述符 (教程 05 全部)
    // ====================================================================

    /**
     * 创建统一缓冲 (UBO)
     * 每个交换链图像配一个 UBO: 因为 GPU 可能同时在渲染多帧,
     * 帧 A 的 UBO 不能被帧 B 覆盖, 所以按图像数量创建多份。
     * 内存属性 HOST_VISIBLE | HOST_COHERENT: CPU 每帧直接写入。
     */
    void createUniformBuffers();

    /**
     * 更新统一缓冲内容 (每帧调用)
     * 计算旋转动画的 MVP 矩阵并写入当前帧图像的 UBO。
     * @param currentImage 当前交换链图像索引
     */
    void updateUniformBuffer(uint32_t currentImage);

    /**
     * 创建描述符池 (VkDescriptorPool)
     * 描述符集需要从描述符池分配, 池声明了可容纳的描述符数量:
     * 每种类型 (UBO / 采样器) × 交换链图像数。
     * FREE_DESCRIPTOR_SET 标志允许单独释放描述符集 (交换链重建时用)。
     */
    void createDescriptorPool();

    /**
     * 创建描述符集 (VkDescriptorSet) 并绑定实际资源
     * 每个交换链图像一个描述符集, 把:
     *  - binding 0: 该图像的 UBO (MVP 矩阵)
     *  - binding 1: 纹理采样器 + 纹理图像视图
     * 写进去, 这样着色器就能通过描述符访问这些资源。
     */
    void createDescriptorSets();

    // ====================================================================
    // 二十一、命令缓冲与渲染 (教程 06 全部)
    // ====================================================================

    /**
     * 创建命令缓冲 (每个交换链图像一个)
     * 命令缓冲记录 GPU 要执行的命令 (绘制等), 提交到队列后执行。
     * 每个图像配一个命令缓冲, 内容在每帧重新记录。
     */
    void createCommandBuffers();

    /**
     * 记录命令缓冲内容 (每个图像一条完整的绘制命令序列)
     * 流程: 开始渲染通道 -> 绑定管线 -> 设置视口/裁剪 ->
     *       绑定顶点/索引缓冲 -> 绑定描述符集 -> 绘制 -> 结束渲染通道
     * @param commandBuffer 待记录的命令缓冲
     * @param imageIndex 交换链图像索引 (决定用哪个帧缓冲)
     */
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    // ====================================================================
    // 二十二、同步对象与逐帧渲染 (教程 06-02 / 06-03)
    // ====================================================================

    /**
     * 创建同步对象
     * GPU 是异步执行的, 需要同步原语协调:
     *  - 信号量 (VkSemaphore): GPU 内部 / GPU 与呈现引擎之间的同步
     *    * imageAvailableSemaphores: "图像已获取, 可以开始渲染"
     *    * renderFinishedSemaphores: "渲染已完成, 可以开始呈现"
     *  - 围栏 (VkFence): CPU 与 GPU 之间的同步 (CPU 可等待)
     *    * inFlightFences: "该帧已完全执行完毕, 可以复用它的资源"
     */
    void createSyncObjects();

    /**
     * 绘制一帧 (每帧调用的核心函数)
     * 流程:
     *  1. 等待当前帧的围栏 (确保上一轮复用该帧资源的渲染已完成)
     *  2. 从交换链获取一张可用图像
     *  3. 等待该图像上的旧围栏 (若它还在被别的帧使用)
     *  4. 更新 UBO (MVP 旋转矩阵)
     *  5. 重置并重新记录命令缓冲
     *  6. 提交到图形队列 (等待图像可用信号量, 发出渲染完成信号量)
     *  7. 呈现到屏幕 (等待渲染完成信号量)
     *  8. 若交换链过期 (窗口尺寸变化等), 重建交换链
     */
    void drawFrame();

    // ====================================================================
    // 二十三、交换链重建 (教程 03-04 交换链重建)
    // ====================================================================

    /**
     * 重建交换链
     * 触发时机: 窗口尺寸变化 / 交换链过期 / 窗口最小化后恢复。
     * 流程: 等待窗口有效尺寸 -> 等 GPU 空闲 -> 销毁所有依赖交换链的资源
     *       -> 按新尺寸全部重建。
     */
    void recreateSwapChain();

    /**
     * 销毁所有依赖交换链的资源 (重建前调用, 也用于最终清理)
     * 包括: 帧缓冲 / 命令缓冲 / 管线 / 深度资源 / 统一缓冲 /
     *       描述符池 / 图像视图 / 交换链本身
     */
    void cleanupSwapChain();

    // ====================================================================
    // 二十四、全局清理
    // ====================================================================

    /**
     * 释放所有资源 (销毁顺序与创建顺序相反)
     * 必须等 GPU 空闲 (mainLoop 末尾已调用 vkDeviceWaitIdle)。
     */
    void cleanup();
};
