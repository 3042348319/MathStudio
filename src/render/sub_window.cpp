// ====================================================================
// sub_window.cpp —— 子窗口实现 (GLFW 窗口 + Vulkan 视口)
// ====================================================================

// 与 viewport_renderer.cpp 一致: Vulkan 深度范围 [0, 1]。
// 必须在包含 glm 之前定义, 否则点击拾取使用的投影矩阵会和渲染不一致。
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "sub_window.h"

#include "coordinate_system_plugin.h"
#include "code_plugin.h"
#include "dynamic_plugin_manager.h"
#include "app_icon.h"
#include "file_dialog.h"
#include "menu.h"
#include "parameter_plugin.h"
#include "particle_plugin.h"
#include "plugin.h"
#include "plugin_editor_io.h"
#include "plugin_factory.h"
#include "plugin_registry.h"
#include "settings.h"
#include "sine_motion_plugin.h"
#include "sine_particle_plugin.h"
#include "theme.h"
#include "viewport_context.h"
#include "viewport_renderer.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// ====================================================================
// SubWindow::Impl —— GLFW 窗口 + ViewportRenderer
// ====================================================================

struct SubWindow::Impl {
    std::shared_ptr<const ViewportContext> context;
    SubWindow* owner = nullptr;
    GLFWwindow* window = nullptr;
    std::unique_ptr<ViewportRenderer> renderer;
    int width = 640;
    int height = 480;
    std::vector<ObjectInstance> instances;  // 复用, 避免每帧分配
    std::vector<ObjectInstance> selectedInstances;  // 选中物体的描边实例
    ViewportCamera camera;                  // 每个子窗口独立相机
    ViewportCamera perspectiveCamera;       // 进入正交视图前的透视相机
    bool hasPerspectiveCamera = false;
    std::string viewIndicator;              // 正交视图右上角指示字

    // ---- 子窗口自己的 ImGui 浮窗 ----
    // 每个子窗口使用独立的 ImGuiContext, 这样可以把参数浮窗渲染到
    // 自己的交换链/renderPass 中, 不影响主菜单的 ImGui 上下文。
    ImGuiContext* imguiContext = nullptr;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    uint32_t imguiRenderPassVersion = 0;
    bool imguiBackendReady = false;
    bool imguiGlfwReady = false;
    unsigned appliedThemeRevision = 0;

    // 顶部菜单栏占用的高度; 参数浮窗和 3D 视口从其下方开始。
    float menuBarHeight = 0.0f;

    // ---- 设置窗口 ----
    bool settingsVisible = false;
    bool settingsBuffersReady = false;
    bool settingsScrollToTop = false;
    std::array<std::array<char, 64>, 8> shortcutBuffers{};
    std::string settingsStatus;

    // 浮窗状态
    int selectedObject = 0;
    bool hasSelection = false;
    bool panelCollapsed = false;
    bool panelScrollToTop = false;
    ImVec2 panelPos{12.0f, 12.0f};
    ImVec2 panelSize{400.0f, 560.0f};

    // 浮窗停靠方式: 浮动 / 贴左 / 贴右 / 贴顶 / 贴底
    enum class PanelDock {
        Floating,
        Left,
        Right,
        Top,
        Bottom,
    };
    PanelDock dockMode = PanelDock::Floating;
    bool dockDragging = false;
    bool dockDragArmed = false;
    PanelDock dockPreview = PanelDock::Floating;
    bool dockLayoutPending = false;
    bool forceFloatingPosition = false;

    // 3D 场景在交换链中的矩形区域 (停靠时是浮窗之外的剩余区域)
    VkRect2D sceneViewport{};
    ImVec2 scenePos{0.0f, 0.0f};
    ImVec2 sceneSize{640.0f, 480.0f};

    // 资源管理器 UI 状态
    std::uint64_t activeGroup = 0;       // 0 = 未分组
    std::uint64_t renamingGroup = 0;     // 正在重命名的组
    ObjectId renamingObject = 0;         // 正在重命名的对象
    char renameBuffer[128] = {};
    ObjectId pendingDeleteObject = 0;
    std::uint64_t pendingDeleteGroup = 0;
    ObjectId pendingMoveObject = 0;
    std::uint64_t pendingMoveGroup = 0;
    bool hasPendingMove = false;
    std::string importStatus;

    // ---- 插件代码编辑器 ----
    bool pluginEditorVisible = false;
    bool editorLayoutPending = false;
    bool editorStaticMode = false;
    int editorObjectIndex = 0;
    int editorPluginIndex = -1;

    /**
     * 编辑器支持多个文件同时打开。
     * 每个文件独立保存 buffer 和 dirty 标记, 标签栏切换。
     */
    struct EditorDocument {
        std::filesystem::path path;
        std::string title;
        std::vector<char> buffer;
        bool dirty = false;
    };
    std::vector<EditorDocument> editorDocuments;
    int activeEditorDocument = -1;

    // 关闭/创建确认弹窗的挂起动作
    bool editorClosePrompt = false;
    bool closeTabPrompt = false;
    int pendingCloseTab = -1;
    bool closeProjectAfterEditorPrompt = false;
    bool projectClosePrompt = false;
    enum class PendingCreate {
        None,
        StaticPlugin,
        DynamicPlugin,
    };
    PendingCreate pendingCreate = PendingCreate::None;
    bool createPluginPrompt = false;

    char newPluginClass[64] = "MyPlugin";
    char newPluginFile[64] = "my_plugin";
    std::string editorStatus;

    // ---- 子窗口时间轴 ----
    // timelineTime 是绝对动画时间; 插件通过 setTime() 定位到该时间。
    float timelineTime = 0.0f;
    float timelineDuration = 12.0f;
    bool timelinePaused = false;
    bool timelineVisible = false;

    // 鼠标轨道/平移状态
    bool rotating = false;
    bool panning = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    Impl(std::shared_ptr<const ViewportContext> sharedContext,
         SubWindow* ownerWindow,
         int windowWidth,
         int windowHeight)
        : context(std::move(sharedContext)),
          owner(ownerWindow),
          width(windowWidth),
          height(windowHeight) {}

    ~Impl() {
        // ImGui 后端依赖 renderPass/device, 必须先于 renderer 销毁。
        shutdownImGui();

        // 先销毁 Vulkan 资源, 再销毁 GLFW 窗口。
        renderer.reset();
        if (window != nullptr) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
    }

    bool create() {
        if (window != nullptr) {
            return true;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        // 先隐藏创建, 等 Vulkan 交换链/管线准备好后再显示, 避免白屏闪烁。
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        window = glfwCreateWindow(width, height, owner->title_.c_str(),
                                  nullptr, nullptr);
        if (window == nullptr) {
            return false;
        }

        applyAppIcon(window);
        applyDarkWindowFrame(window);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetKeyCallback(window, keyCallback);
        glfwSetCharCallback(window, charCallback);
        glfwSetWindowFocusCallback(window, focusCallback);

        try {
            renderer = std::make_unique<ViewportRenderer>(context, window);
            // ImGui 的 Vulkan 管线需要 renderer 的 renderPass, 因此放在后面。
            initImGui();
        } catch (...) {
            shutdownImGui();
            renderer.reset();
            glfwDestroyWindow(window);
            window = nullptr;
            throw;
        }
        return true;
    }

    bool show() {
        if (!create()) {
            return false;
        }
        glfwShowWindow(window);
        glfwFocusWindow(window);
        return true;
    }

    void hide() {
        if (window != nullptr) {
            glfwHideWindow(window);
        }
    }

    bool visible() const {
        return window != nullptr &&
               glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0;
    }

    void setTitle(const std::string& title) {
        if (window != nullptr) {
            glfwSetWindowTitle(window, title.c_str());
        }
    }

    // ================================================================
    // ImGui 浮窗
    // ================================================================

    /**
     * 初始化子窗口自己的 ImGui 上下文
     *
     * 注意: 每个子窗口一个 ImGuiContext。因为每个子窗口的
     * renderPass/swapchain 都不同, ImGui 的 Vulkan 管线不能共用。
     */
    void initImGui() {
        if (imguiContext != nullptr) {
            return;
        }

        // ---- 字体纹理描述符池 ----
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(context->device, &poolInfo, nullptr,
                                   &imguiDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("创建子窗口 ImGui 描述符池失败!");
        }

        // ---- 上下文 ----
        // 注意: ImGui::CreateContext() 只有在当前没有上下文时才会自动
        // 切换当前上下文。主窗口已经有一个上下文, 因此这里必须显式
        // 保存 CreateContext() 的返回值并切换过去。
        ImGuiContext* previous = ImGui::GetCurrentContext();
        imguiContext = ImGui::CreateContext();
        if (imguiContext == nullptr) {
            throw std::runtime_error("创建子窗口 ImGui 上下文失败!");
        }

        ImGui::SetCurrentContext(imguiContext);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        // 每个子窗口使用自己的 ImGui 上下文, 因此也要应用同一套主题。
        applyMathStudioTheme();
        appliedThemeRevision = uiSettingsRevision();

        // ---- 中文字体 ----
        // 浮窗字符较少, 使用常用字范围即可, 避免每个子窗口都生成
        // 一份完整的 CJK 字体图集。
        ImFont* font = nullptr;
        const char* fontPath = "C:\\Windows\\Fonts\\simhei.ttf";
        std::error_code error;
        if (std::filesystem::exists(fontPath, error)) {
            font = io.Fonts->AddFontFromFileTTF(
                fontPath,
                16.0f,
                nullptr,
                mathStudioGlyphRanges());
        }
        if (font == nullptr) {
            io.Fonts->AddFontDefault();
        }

        // ---- GLFW 平台后端 ----
        // install_callbacks = false: 输入由下面的 GLFW 回调手动转发,
        // 避免多个 ImGuiContext 的回调互相干扰。
        if (!ImGui_ImplGlfw_InitForVulkan(window, false)) {
            throw std::runtime_error("初始化子窗口 ImGui GLFW 后端失败!");
        }
        imguiGlfwReady = true;

        // ---- Vulkan 渲染后端 ----
        initImGuiVulkanBackend();
        imguiBackendReady = true;

        if (!ImGui_ImplVulkan_CreateFontsTexture()) {
            throw std::runtime_error("创建子窗口 ImGui 字体纹理失败!");
        }

        imguiRenderPassVersion = renderer->renderPassVersion();

        ImGui::SetCurrentContext(previous);
    }

    /**
     * 用当前 renderPass 初始化 ImGui Vulkan 后端
     * 调用前必须把 imguiContext 设为当前上下文。
     */
    void initImGuiVulkanBackend() {
        ImGui_ImplVulkan_InitInfo info{};

        info.ApiVersion = VK_API_VERSION_1_0;
        info.Instance = context->instance;
        info.PhysicalDevice = context->physicalDevice;
        info.Device = context->device;
        info.QueueFamily = context->graphicsFamily;
        info.Queue = context->graphicsQueue;
        info.DescriptorPool = imguiDescriptorPool;
        info.RenderPass = renderer->renderPass();
        info.Subpass = 0;
        info.MinImageCount =
            std::max(2u, renderer->imageCount());
        info.ImageCount = info.MinImageCount;
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.PipelineCache = VK_NULL_HANDLE;

        if (!ImGui_ImplVulkan_Init(&info)) {
            throw std::runtime_error("初始化子窗口 ImGui Vulkan 后端失败!");
        }
    }

    /**
     * 交换链重建后重新创建 ImGui Vulkan 管线
     * ImGui 的管线绑定在 renderPass 上, renderPass 变了就必须重建。
     */
    void recreateImGuiVulkanBackend() {
        if (imguiContext == nullptr) {
            return;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);

        if (imguiBackendReady) {
            ImGui_ImplVulkan_Shutdown();
            imguiBackendReady = false;
        }

        initImGuiVulkanBackend();
        imguiBackendReady = true;

        if (!ImGui_ImplVulkan_CreateFontsTexture()) {
            throw std::runtime_error("重建子窗口 ImGui 字体纹理失败!");
        }

        imguiRenderPassVersion = renderer->renderPassVersion();

        ImGui::SetCurrentContext(previous);
    }

    /** 关闭 ImGui, 必须在 renderer/window 销毁之前调用 */
    void shutdownImGui() {
        if (imguiContext == nullptr) {
            if (imguiDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(context->device,
                                        imguiDescriptorPool, nullptr);
                imguiDescriptorPool = VK_NULL_HANDLE;
            }
            return;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);

        if (imguiBackendReady) {
            ImGui_ImplVulkan_Shutdown();
            imguiBackendReady = false;
        }
        if (imguiGlfwReady) {
            ImGui_ImplGlfw_Shutdown();
            imguiGlfwReady = false;
        }

        ImGuiContext* destroyed = imguiContext;
        ImGui::DestroyContext(imguiContext);
        imguiContext = nullptr;

        if (imguiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(context->device,
                                    imguiDescriptorPool, nullptr);
            imguiDescriptorPool = VK_NULL_HANDLE;
        }

        ImGui::SetCurrentContext(previous == destroyed ? nullptr : previous);
    }

    // ----------------------------------------------------------------
    // 浮窗布局
    // ----------------------------------------------------------------

    /** Object 自身的参数 (与插件参数分开显示) */
    void drawObjectSection(Object& object) {
        uiSectionGap(4.0f);

        ImGui::Text("网格: %s", object.mesh().name().c_str());
        ImGui::TextDisabled("顶点: %zu    索引: %zu",
                            object.mesh().vertices().size(),
                            object.mesh().indices().size());

        float position[3] = {
            object.position().x,
            object.position().y,
            object.position().z
        };
        if (ImGui::DragFloat3("坐标", position, 0.01f)) {
            object.setPosition(
                glm::vec3(position[0], position[1], position[2]));
            markProjectDirty();
        }

        float color[4] = {
            object.color().r,
            object.color().g,
            object.color().b,
            object.color().a
        };
        if (ImGui::ColorEdit4("颜色", color,
                              ImGuiColorEditFlags_AlphaBar)) {
            object.setColor(
                glm::vec4(color[0], color[1], color[2], color[3]));
            markProjectDirty();
        }
    }

    /** 插件参数: 每个插件一个可折叠分组 */
    void drawPluginSection(Object& object) {
        uiSectionTitle("◆", "插件参数");

        const auto& plugins = object.plugins();
        if (plugins.empty()) {
            return;
        }

        for (std::size_t i = 0; i < plugins.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));

            Plugin* plugin = plugins[i].get();

            // 箭头由 CollapsingHeader 绘制在左侧; 图标放在箭头之后,
            // 文字再缩进, 避免图标与箭头/标题重叠。
            const std::string header =
                "        " + plugin->name() + "###plugin";
            const bool pluginOpen = ImGui::CollapsingHeader(
                header.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen);
            drawPluginKindIcon(
                *plugin, ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax(),
                ImGui::GetTreeNodeToLabelSpacing() + 9.0f);
            if (pluginOpen) {

                if (auto* particle =
                        dynamic_cast<ParticlePlugin*>(plugin)) {
                    float velocity[3] = {
                        particle->velocity().x,
                        particle->velocity().y,
                        particle->velocity().z
                    };
                    if (ImGui::DragFloat3("速度", velocity, 0.01f)) {
                        particle->setVelocity(
                            glm::vec3(velocity[0],
                                      velocity[1],
                                      velocity[2]));
                    }

                    float lifetime = particle->lifetime();
                    if (ImGui::DragFloat("生命周期", &lifetime,
                                         0.05f, 0.01f, 60.0f)) {
                        particle->setLifetime(lifetime);
                    }

                    float size = particle->size();
                    if (ImGui::DragFloat("尺寸", &size,
                                         0.005f, 0.0f, 5.0f)) {
                        particle->setSize(size);
                    }

                    ImGui::TextDisabled("当前年龄: %.2f s",
                                        particle->age());
                } else if (auto* coordinate =
                               dynamic_cast<CoordinateSystemPlugin*>(plugin)) {
                    float axisLength = coordinate->axisLength();
                    if (ImGui::DragFloat("轴长", &axisLength,
                                         0.02f, 0.01f, 20.0f)) {
                        coordinate->setAxisLength(axisLength);
                    }

                    bool showGrid = coordinate->showGrid();
                    if (ImGui::Checkbox("显示网格", &showGrid)) {
                        coordinate->setShowGrid(showGrid);
                    }
                } else if (auto* sine =
                               dynamic_cast<SineParticlePlugin*>(plugin)) {
                    uiSectionTitle("◆", "正弦粒子");

                    float amplitude = sine->amplitude();
                    if (ImGui::DragFloat("振幅", &amplitude,
                                         0.01f, 0.0f, 5.0f)) {
                        sine->setAmplitude(amplitude);
                    }

                    float pathLength = sine->pathLength();
                    if (ImGui::DragFloat("轨道长度", &pathLength,
                                         0.02f, 0.01f, 10.0f)) {
                        sine->setPathLength(pathLength);
                    }

                    float frequency = sine->frequency();
                    if (ImGui::DragFloat("频率", &frequency,
                                         0.02f, -20.0f, 20.0f)) {
                        sine->setFrequency(frequency);
                    }

                    float speed = sine->speed();
                    if (ImGui::DragFloat("速度", &speed,
                                         0.01f, -10.0f, 10.0f)) {
                        sine->setSpeed(speed);
                    }

                    float phase = sine->phase();
                    if (ImGui::DragFloat("相位", &phase,
                                         0.01f, -20.0f, 20.0f)) {
                        sine->setPhase(phase);
                    }

                    float size = sine->size();
                    if (ImGui::DragFloat("粒子尺寸", &size,
                                         0.005f, 0.001f, 2.0f)) {
                        sine->setSize(size);
                    }

                    float trailDuration = sine->trailDuration();
                    if (ImGui::DragFloat("轨迹时长", &trailDuration,
                                         0.01f, 0.0f, 10.0f)) {
                        sine->setTrailDuration(trailDuration);
                    }

                    int trailSamples = sine->trailSamples();
                    if (ImGui::SliderInt("轨迹点数", &trailSamples,
                                         2, 120)) {
                        sine->setTrailSamples(trailSamples);
                    }

                    float trailSize = sine->trailPointSize();
                    if (ImGui::DragFloat("轨迹点大小", &trailSize,
                                         0.001f, 0.001f, 0.5f)) {
                        sine->setTrailPointSize(trailSize);
                    }
                } else if (auto* motion =
                               dynamic_cast<SineMotionPlugin*>(plugin)) {
                    uiSectionTitle("◆", "正弦运动");

                    float horizontal = motion->horizontalAmplitude();
                    if (ImGui::DragFloat("水平振幅", &horizontal,
                                         0.01f, 0.0f, 5.0f)) {
                        motion->setHorizontalAmplitude(horizontal);
                    }

                    float vertical = motion->verticalAmplitude();
                    if (ImGui::DragFloat("垂直振幅", &vertical,
                                         0.01f, 0.0f, 5.0f)) {
                        motion->setVerticalAmplitude(vertical);
                    }

                    float frequency = motion->frequency();
                    if (ImGui::DragFloat("曲线频率", &frequency,
                                         0.02f, -20.0f, 20.0f)) {
                        motion->setFrequency(frequency);
                    }

                    float speed = motion->speed();
                    if (ImGui::DragFloat("运动速度", &speed,
                                         0.01f, -10.0f, 10.0f)) {
                        motion->setSpeed(speed);
                    }

                    float phase = motion->phase();
                    if (ImGui::DragFloat("相位", &phase,
                                         0.01f, -20.0f, 20.0f)) {
                        motion->setPhase(phase);
                    }
                } else if (plugin->parameterCount() > 0) {
                    // 动态 DLL 或覆写了通用参数接口的静态插件。
                    const std::size_t count = plugin->parameterCount();
                    for (std::size_t parameterIndex = 0;
                         parameterIndex < count; ++parameterIndex) {
                        const char* name =
                            plugin->parameterName(parameterIndex);
                        if (name == nullptr || name[0] == '\0') {
                            continue;
                        }

                        ImGui::PushID(static_cast<int>(parameterIndex));
                        float value =
                            plugin->parameterValue(parameterIndex);
                        if (ImGui::DragFloat(name, &value, 0.01f)) {
                            plugin->setParameterValue(parameterIndex,
                                                      value);
                            markProjectDirty();
                        }
                        ImGui::PopID();
                    }
                } else if (auto* parameter =
                               dynamic_cast<ParameterPlugin*>(plugin)) {
                    char value[256] = {};
                    std::snprintf(value, sizeof(value), "%s",
                                  parameter->value().c_str());

                    if (ImGui::InputText("值", value, sizeof(value))) {
                        parameter->setValue(value);
                    }
                } else {
                    // 插件摘要属于说明性文字, 不显示在窗口里。
                }
            }

            ImGui::PopID();
        }
    }

    // ----------------------------------------------------------------
    // 资源管理器: 组别 / 对象列表
    // ----------------------------------------------------------------

    void beginRenameGroup(std::uint64_t groupId) {
        for (const ObjectGroup& group : owner->hierarchy_.groups()) {
            if (group.id == groupId) {
                std::snprintf(renameBuffer, sizeof(renameBuffer), "%s",
                              group.name.c_str());
                renamingGroup = groupId;
                renamingObject = 0;
                return;
            }
        }
    }

    void beginRenameObject(ObjectId objectId) {
        const Object* object = owner->objectById(objectId);
        if (object == nullptr) {
            return;
        }

        std::snprintf(renameBuffer, sizeof(renameBuffer), "%s",
                      object->name().c_str());
        renamingObject = objectId;
        renamingGroup = 0;
    }

    /** 绘制睁眼/闭眼图标; 返回图标中心, 便于调用方绘制 */
    void drawEyeIcon(bool visible, const ImVec2& itemMin,
                     const ImVec2& itemMax) const {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 center((itemMin.x + itemMax.x) * 0.5f,
                            (itemMin.y + itemMax.y) * 0.5f);
        const ImU32 color = visible
                                ? IM_COL32(220, 224, 232, 255)
                                : IM_COL32(120, 124, 132, 255);

        if (visible) {
            // 上下两条弧组成眼眶, 中间画瞳孔。
            drawList->AddBezierCubic(
                ImVec2(center.x - 8.0f, center.y),
                ImVec2(center.x - 4.0f, center.y - 5.0f),
                ImVec2(center.x + 4.0f, center.y - 5.0f),
                ImVec2(center.x + 8.0f, center.y), color, 1.4f);
            drawList->AddBezierCubic(
                ImVec2(center.x + 8.0f, center.y),
                ImVec2(center.x + 4.0f, center.y + 5.0f),
                ImVec2(center.x - 4.0f, center.y + 5.0f),
                ImVec2(center.x - 8.0f, center.y), color, 1.4f);
            drawList->AddCircleFilled(center, 2.0f, color, 12);
            return;
        }

        // 闭眼: 一条向下的弧加两根短睫毛。
        drawList->AddBezierCubic(
            ImVec2(center.x - 8.0f, center.y - 1.0f),
            ImVec2(center.x - 4.0f, center.y + 4.0f),
            ImVec2(center.x + 4.0f, center.y + 4.0f),
            ImVec2(center.x + 8.0f, center.y - 1.0f),
            color, 1.4f);
        drawList->AddLine(ImVec2(center.x - 5.0f, center.y + 4.0f),
                          ImVec2(center.x - 6.0f, center.y + 7.0f),
                          color, 1.2f);
        drawList->AddLine(ImVec2(center.x, center.y + 5.0f),
                          ImVec2(center.x, center.y + 8.0f), color,
                          1.2f);
        drawList->AddLine(ImVec2(center.x + 5.0f, center.y + 4.0f),
                          ImVec2(center.x + 6.0f, center.y + 7.0f),
                          color, 1.2f);
    }

    /** 资源管理器中的一行对象 */
    void drawObjectRow(ObjectId objectId) {        Object* object = owner->objectById(objectId);
        if (object == nullptr) {
            return;
        }

        const std::size_t index = owner->indexOfObject(objectId);
        if (index >= owner->objects_.size()) {
            return;
        }

        ImGui::PushID(static_cast<int>(objectId & 0x7fffffff));

        if (renamingObject == objectId) {
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##object_name", renameBuffer,
                                 sizeof(renameBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                object->setName(renameBuffer);
                renamingObject = 0;
            }
            if (ImGui::IsItemDeactivated()) {
                renamingObject = 0;
            }
        } else {
            // 眼睛图标: 睁眼显示, 闭眼隐藏。
            if (ImGui::InvisibleButton("##object_visible",
                                       ImVec2(22.0f, 22.0f))) {
                object->setVisible(!object->visible());
                markProjectDirty();
            }
            drawEyeIcon(object->visible(), ImGui::GetItemRectMin(),
                        ImGui::GetItemRectMax());
            ImGui::SameLine(0.0f, 3.0f);

            const bool selected =
                (static_cast<int>(index) == selectedObject);
            const std::string label =
                object->name() + "  (" + object->typeName() + ")";

            if (ImGui::Selectable(label.c_str(), selected,
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, 22.0f))) {
                selectedObject = static_cast<int>(index);
                hasSelection = true;
                panelScrollToTop = true;
            }

            // 拖动对象到组节点即可移动分组。
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("OBJECT_ID", &objectId,
                                          sizeof(ObjectId));
                ImGui::Text("移动 %s", object->name().c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginPopupContextItem("##object_context")) {
                if (ImGui::MenuItem("→ 重命名")) {
                    beginRenameObject(objectId);
                }

                if (ImGui::BeginMenu("→ 移动到组")) {
                    const auto current =
                        owner->hierarchy_.groupOf(objectId);

                    if (ImGui::MenuItem("□ 未分组", nullptr,
                                        !current.has_value())) {
                        pendingMoveObject = objectId;
                        pendingMoveGroup = 0;
                        hasPendingMove = true;
                    }

                    for (const ObjectGroup& group :
                         owner->hierarchy_.groups()) {
                        const bool inGroup =
                            current.has_value() &&
                            *current == group.id;
                        if (ImGui::MenuItem(group.name.c_str(), nullptr,
                                            inGroup)) {
                            pendingMoveObject = objectId;
                            pendingMoveGroup = group.id;
                            hasPendingMove = true;
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                if (ImGui::MenuItem("× 删除")) {
                    pendingDeleteObject = objectId;
                }
                ImGui::EndPopup();
            }
        }

        ImGui::PopID();
    }

    /** 上半区: 可滚动的 Object / 组资源管理器 */
    void drawHierarchy(float height) {
        ImGui::BeginChild("##object_hierarchy", ImVec2(0.0f, height),
                          true);

        uiSectionTitle("◆", "对象资源");

        if (ImGui::Button("＋ 组", ImVec2(62.0f, 24.0f))) {
            const std::string name =
                "新组 " +
                std::to_string(owner->hierarchy_.groups().size() + 1);
            const std::uint64_t groupId =
                owner->hierarchy_.createGroup(name);
            activeGroup = groupId;
            beginRenameGroup(groupId);
        }

        ImGui::SameLine();
        if (ImGui::Button("↑ 导入 OBJ", ImVec2(104.0f, 24.0f))) {
            importObjFile();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%zu 个对象", owner->objects_.size());

        if (!importStatus.empty()) {
            ImGui::TextUnformatted(importStatus.c_str());
        }

        uiSectionGap(8.0f);

        // ---- 组别 ----
        for (const ObjectGroup& group : owner->hierarchy_.groups()) {
            ImGui::PushID(static_cast<int>(group.id & 0x7fffffff));

            if (renamingGroup == group.id) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##group_name", renameBuffer,
                                     sizeof(renameBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    owner->hierarchy_.renameGroup(group.id, renameBuffer);
                    renamingGroup = 0;
                }
                if (ImGui::IsItemDeactivated()) {
                    renamingGroup = 0;
                }
            } else {
                const ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_DefaultOpen |
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanAvailWidth;

                const bool open =
                    ImGui::TreeNodeEx(group.name.c_str(), flags);

                if (ImGui::IsItemClicked()) {
                    activeGroup = group.id;
                }

                // 组节点是拖放目标: 把对象拖到这里即可加入该组。
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload("OBJECT_ID")) {
                        pendingMoveObject =
                            *static_cast<const ObjectId*>(payload->Data);
                        pendingMoveGroup = group.id;
                        hasPendingMove = true;
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("##group_context")) {
                    if (ImGui::MenuItem("→ 重命名")) {
                        beginRenameGroup(group.id);
                    }
                    if (ImGui::MenuItem("× 删除组")) {
                        pendingDeleteGroup = group.id;
                    }
                    ImGui::EndPopup();
                }

                if (open) {
                    if (group.objects.empty()) {
                    } else {
                        for (ObjectId objectId : group.objects) {
                            drawObjectRow(objectId);
                        }
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::PopID();
        }

        // ---- 未分组 ----
        const ImGuiTreeNodeFlags ungroupedFlags =
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth;

        const bool ungroupedOpen =
            ImGui::TreeNodeEx("未分组", ungroupedFlags);
        if (ImGui::IsItemClicked()) {
            activeGroup = 0;
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("OBJECT_ID")) {
                pendingMoveObject =
                    *static_cast<const ObjectId*>(payload->Data);
                pendingMoveGroup = 0;
                hasPendingMove = true;
            }
            ImGui::EndDragDropTarget();
        }
        if (ungroupedOpen) {
            if (owner->hierarchy_.ungrouped().empty()) {
            } else {
                for (ObjectId objectId : owner->hierarchy_.ungrouped()) {
                    drawObjectRow(objectId);
                }
            }
            ImGui::TreePop();
        }

        ImGui::EndChild();

        // ---- 延迟处理删除/移动, 避免遍历容器时修改容器 ----
        if (pendingDeleteObject != 0) {
            const std::size_t index =
                owner->indexOfObject(pendingDeleteObject);
            if (index < owner->objects_.size()) {
                owner->removeObject(index);
                if (owner->objects_.empty()) {
                    selectedObject = 0;
                    hasSelection = false;
                } else {
                    selectedObject = std::clamp(
                        selectedObject, 0,
                        static_cast<int>(owner->objects_.size()) - 1);
                }
            }
            pendingDeleteObject = 0;
        }

        if (pendingDeleteGroup != 0) {
            owner->hierarchy_.removeGroup(pendingDeleteGroup);
            if (activeGroup == pendingDeleteGroup) {
                activeGroup = 0;
            }
            pendingDeleteGroup = 0;
        }

        if (hasPendingMove) {
            owner->hierarchy_.moveObject(
                pendingMoveObject,
                pendingMoveGroup == 0
                    ? std::nullopt
                    : std::optional<std::uint64_t>(pendingMoveGroup));
            hasPendingMove = false;
            pendingMoveObject = 0;
            pendingMoveGroup = 0;
        }
    }

    /** 选择 .obj 并导入为新的 Object */
    void importObjFile() {
        const std::string path = openObjFileDialog();
        if (path.empty()) {
            return;
        }

        try {
            Mesh mesh = Mesh::loadFromObjFile(path);
            const std::string name = mesh.name();

            Object& object = owner->addObject<Object>(
                std::move(mesh),
                glm::vec3(0.0f),
                glm::vec4(0.80f, 0.84f, 1.0f, 1.0f),
                name);

            if (activeGroup != 0) {
                owner->hierarchy_.moveObject(object.id(), activeGroup);
            }

            selectedObject =
                static_cast<int>(owner->indexOfObject(object.id()));
            hasSelection = true;
            panelScrollToTop = true;
            importStatus.clear();
        } catch (const std::exception& error) {
            importStatus = error.what();
        }
    }

    /** 判断鼠标释放位置靠近哪条边 */
    PanelDock pickDockFromMouse(const ImVec2& mouse) const {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        constexpr float threshold = 70.0f;

        if (mouse.x <= threshold) {
            return PanelDock::Left;
        }
        if (mouse.x >= displaySize.x - threshold) {
            return PanelDock::Right;
        }
        if (mouse.y <= threshold) {
            return PanelDock::Top;
        }
        if (mouse.y >= displaySize.y - threshold) {
            return PanelDock::Bottom;
        }
        return PanelDock::Floating;
    }

    /** 拖动结束: 应用停靠目标; 中间区域恢复浮动 */
    void applyDockTarget(PanelDock target, const ImVec2& mouse) {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;

        if (target == PanelDock::Floating) {
            const bool wasDocked = dockMode != PanelDock::Floating;
            dockMode = PanelDock::Floating;

            // 从停靠状态拖回中间时, 让浮窗出现在鼠标附近。
            // 本来就是浮动状态时保持用户刚拖到的位置, 不再跳一下。
            if (wasDocked) {
                const float maxX =
                    std::max(8.0f, displaySize.x - panelSize.x - 8.0f);
                const float maxY =
                    std::max(8.0f, displaySize.y - panelSize.y - 8.0f);

                panelPos.x = std::min(
                    std::max(mouse.x - panelSize.x * 0.5f, 8.0f), maxX);
                panelPos.y = std::min(
                    std::max(mouse.y - 24.0f, 8.0f), maxY);
                forceFloatingPosition = true;
                dockLayoutPending = true;
            }
        } else {
            dockMode = target;
            dockLayoutPending = true;
        }
    }

    /** 拖动时绘制目标停靠区域预览 */
    void drawDockPreview(PanelDock target) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;

        const float sideWidth = std::clamp(
            panelSize.x, 260.0f, displaySize.x * 0.60f);
        const float sideHeight = std::clamp(
            panelSize.y, 220.0f, displaySize.y * 0.60f);

        ImVec2 min(0.0f, 0.0f);
        ImVec2 max(0.0f, 0.0f);
        const char* label = "";

        switch (target) {
        case PanelDock::Left:
            min = ImVec2(0.0f, 0.0f);
            max = ImVec2(sideWidth, displaySize.y);
            label = "停靠左侧";
            break;
        case PanelDock::Right:
            min = ImVec2(displaySize.x - sideWidth, 0.0f);
            max = ImVec2(displaySize.x, displaySize.y);
            label = "停靠右侧";
            break;
        case PanelDock::Top:
            min = ImVec2(0.0f, 0.0f);
            max = ImVec2(displaySize.x, sideHeight);
            label = "停靠顶部";
            break;
        case PanelDock::Bottom:
            min = ImVec2(0.0f, displaySize.y - sideHeight);
            max = ImVec2(displaySize.x, displaySize.y);
            label = "停靠底部";
            break;
        case PanelDock::Floating:
            min = ImVec2(displaySize.x * 0.5f - sideWidth * 0.5f,
                         displaySize.y * 0.5f - sideHeight * 0.5f);
            max = ImVec2(displaySize.x * 0.5f + sideWidth * 0.5f,
                         displaySize.y * 0.5f + sideHeight * 0.5f);
            label = "恢复浮动";
            break;
        }

        drawList->AddRectFilled(min, max, IM_COL32(232, 116, 23, 55), 8.0f);
        drawList->AddRect(min, max, IM_COL32(255, 158, 55, 235),
                          8.0f, 0, 2.0f);
        drawList->AddText(ImVec2(min.x + 12.0f, min.y + 10.0f),
                          IM_COL32(255, 255, 255, 235), label);
    }

    /** 标记工程有未保存修改 */
    void markProjectDirty() {
        if (owner->menu() != nullptr) {
            owner->menu()->markProjectDirty();
        }
    }

    /**
     * 插件列表类型图标:
     *   静态插件 -> 蓝色正方形
     *   动态插件 -> 橙色圆形
     */
    void drawPluginKindIcon(const Plugin& plugin,
                            const ImVec2& itemMin,
                            const ImVec2& itemMax,
                            float xOffset) const {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float centerY = (itemMin.y + itemMax.y) * 0.5f;
        if (plugin.isDynamic()) {
            drawList->AddCircleFilled(
                ImVec2(itemMin.x + xOffset, centerY), 5.0f,
                IM_COL32(232, 116, 23, 255), 20);
        } else {
            drawList->AddRectFilled(
                ImVec2(itemMin.x + xOffset - 6.0f, centerY - 6.0f),
                ImVec2(itemMin.x + xOffset + 6.0f, centerY + 6.0f),
                IM_COL32(72, 142, 255, 255), 2.0f);
        }
    }

    /** 插件对应的源码文件 */
    std::filesystem::path sourceFileForPlugin(const Plugin& plugin) const {
        if (const auto* code = dynamic_cast<const CodePlugin*>(&plugin)) {
            return std::filesystem::u8path(code->sourceFile());
        }
        return pluginSourceFile(plugin.typeId());
    }

    /** 当前激活的编辑器文件; 没有打开文件返回 nullptr */
    EditorDocument* activeEditorDocumentPtr() {
        if (activeEditorDocument < 0 ||
            activeEditorDocument >=
                static_cast<int>(editorDocuments.size())) {
            return nullptr;
        }
        return &editorDocuments[static_cast<std::size_t>(
            activeEditorDocument)];
    }

    /** 生成 1MB 可编辑缓冲; 文本复制到开头 */
    static std::vector<char> makeEditorBuffer(const std::string& text) {
        std::vector<char> buffer(1024 * 1024, '\0');
        std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
        return buffer;
    }

    /** 打开源码; 如果已打开则直接切换标签
     *  reloadIfOpen=true 时重新从磁盘读入 (创建/覆盖源码后使用)。
     */
    void openEditorDocument(const std::filesystem::path& path,
                            bool reloadIfOpen = false) {
        if (path.empty()) {
            editorStatus = "没有可打开的源码";
            return;
        }

        const std::string pathText = path.u8string();
        for (std::size_t i = 0; i < editorDocuments.size(); ++i) {
            if (editorDocuments[i].path.u8string() == pathText) {
                if (!reloadIfOpen) {
                    activeEditorDocument = static_cast<int>(i);
                    editorStatus = "已切换: " + pathText;
                    return;
                }

                std::string text;
                std::string error;
                if (!readTextFile(path, text, error)) {
                    editorStatus = error;
                    return;
                }
                editorDocuments[i].buffer = makeEditorBuffer(text);
                editorDocuments[i].dirty = false;
                activeEditorDocument = static_cast<int>(i);
                editorStatus = "已重新载入: " + pathText;
                return;
            }
        }

        std::string text;
        std::string error;
        if (!readTextFile(path, text, error)) {
            editorStatus = error;
            return;
        }

        EditorDocument document;
        document.path = path;
        document.title = path.filename().u8string();
        document.buffer = makeEditorBuffer(text);
        document.dirty = false;
        editorDocuments.push_back(std::move(document));
        activeEditorDocument =
            static_cast<int>(editorDocuments.size()) - 1;
        editorStatus = "已打开: " + pathText;
    }

    /**
     * 把“新建插件”的内容放进一个未保存标签。
     *
     * 关键点: 创建时只创建内存文档, 不立即写磁盘。
     * 这样用户还没点击保存时, 文件系统里不会出现同名文件,
     * 第二次创建不会被误判为“已存在”。
     */
    EditorDocument& upsertEditorDocument(
        const std::filesystem::path& path,
        const std::string& text) {
        const std::string pathText = path.u8string();
        for (std::size_t i = 0; i < editorDocuments.size(); ++i) {
            if (editorDocuments[i].path.u8string() == pathText) {
                editorDocuments[i].buffer = makeEditorBuffer(text);
                editorDocuments[i].dirty = true;
                activeEditorDocument = static_cast<int>(i);
                return editorDocuments[i];
            }
        }

        EditorDocument document;
        document.path = path;
        document.title = path.filename().u8string();
        document.buffer = makeEditorBuffer(text);
        document.dirty = true;
        editorDocuments.push_back(std::move(document));
        activeEditorDocument =
            static_cast<int>(editorDocuments.size()) - 1;
        return editorDocuments.back();
    }

    /** 保存当前文件; 返回是否成功 */
    bool saveActiveEditorDocument() {
        EditorDocument* document = activeEditorDocumentPtr();
        if (document == nullptr) {
            editorStatus = "没有打开的文件";
            return false;
        }

        std::string error;
        if (!writeTextFile(document->path, document->buffer.data(),
                           error)) {
            editorStatus = error;
            return false;
        }
        document->dirty = false;
        editorStatus = "已保存: " + document->path.u8string();
        return true;
    }

    bool saveAllEditorDocuments() {
        bool ok = true;
        for (EditorDocument& document : editorDocuments) {
            if (!document.dirty) {
                continue;
            }
            std::string error;
            if (!writeTextFile(document.path, document.buffer.data(),
                               error)) {
                editorStatus = error;
                ok = false;
                continue;
            }
            document.dirty = false;
        }
        if (ok && !editorDocuments.empty()) {
            editorStatus = "已保存全部源码";
        }
        return ok;
    }

    bool saveEditorDocument(int index) {
        if (index < 0 ||
            index >= static_cast<int>(editorDocuments.size())) {
            return false;
        }
        EditorDocument& document =
            editorDocuments[static_cast<std::size_t>(index)];
        std::string error;
        if (!writeTextFile(document.path, document.buffer.data(),
                           error)) {
            editorStatus = error;
            return false;
        }
        document.dirty = false;
        editorStatus = "已保存: " + document.path.u8string();
        return true;
    }

    bool hasDirtyEditorDocuments() const {
        for (const EditorDocument& document : editorDocuments) {
            if (document.dirty) {
                return true;
            }
        }
        return false;
    }

    /** 当前文档是否属于静态/动态插件模式 */
    bool editorDocumentMatchesMode(
        const std::filesystem::path& path) const {
        std::string text = path.u8string();
        std::replace(text.begin(), text.end(), '\\', '/');
        const std::filesystem::path normalized = path.lexically_normal();
        text = normalized.u8string();
        std::replace(text.begin(), text.end(), '\\', '/');

        // 先判断 dynamic_plugins, 避免
        // src/plugins/../dynamic_plugins/... 被误判为静态插件。
        if (text.find("/src/dynamic_plugins/") != std::string::npos) {
            return !editorStaticMode;
        }
        if (text.find("/src/plugins/") != std::string::npos) {
            return editorStaticMode;
        }
        return !editorStaticMode;
    }

    /** 切换模式后保证活动标签属于当前模式 */
    void selectVisibleEditorDocument() {
        if (activeEditorDocument >= 0 &&
            activeEditorDocument <
                static_cast<int>(editorDocuments.size()) &&
            editorDocumentMatchesMode(
                editorDocuments[activeEditorDocument].path)) {
            return;
        }

        activeEditorDocument = -1;
        for (std::size_t i = 0; i < editorDocuments.size(); ++i) {
            if (editorDocumentMatchesMode(editorDocuments[i].path)) {
                activeEditorDocument = static_cast<int>(i);
                return;
            }
        }
    }

    void closeEditorDocument(int index) {
        if (index < 0 ||
            index >= static_cast<int>(editorDocuments.size())) {
            return;
        }

        editorDocuments.erase(editorDocuments.begin() + index);
        if (editorDocuments.empty()) {
            activeEditorDocument = -1;
            return;
        }

        if (activeEditorDocument == index) {
            activeEditorDocument = std::min(
                index, static_cast<int>(editorDocuments.size()) - 1);
        } else if (activeEditorDocument > index) {
            --activeEditorDocument;
        }
    }

    /** 动态插件编译所用的源码根目录 */
    std::filesystem::path compileSourceRoot() const {
        if (const char* value = std::getenv("MATHSTUDIO_SOURCE_DIR")) {
            if (value[0] != '\0') {
                return std::filesystem::u8path(value);
            }
        }

        const std::filesystem::path root = findProjectRoot();
        if (!root.empty() &&
            std::filesystem::exists(root / "CMakeLists.txt")) {
            return root;
        }

        // 开发机默认目录; 发布包可通过 MATHSTUDIO_SOURCE_DIR 覆盖。
        const std::filesystem::path fallback =
            "D:/Program/VulkanProject/math";
        if (std::filesystem::exists(fallback / "CMakeLists.txt")) {
            return fallback;
        }
        return std::filesystem::path();
    }

    std::filesystem::path compileBuildRoot(
        const std::filesystem::path& sourceRoot) const {
        if (const char* value = std::getenv("MATHSTUDIO_BUILD_DIR")) {
            if (value[0] != '\0') {
                return std::filesystem::u8path(value);
            }
        }
        return sourceRoot / "build";
    }

    static std::string quotePath(const std::filesystem::path& path) {
        return "\"" + path.u8string() + "\"";
    }

    /** 创建 EditorPlugin 动态源码 */
    void createDynamicPluginSource() {
        const std::filesystem::path root = compileSourceRoot();
        if (root.empty()) {
            editorStatus = "找不到 CMake 源码目录";
            return;
        }

        const std::string className =
            sanitizeIdentifier(newPluginClass);
        const std::filesystem::path source =
            root / "src" / "dynamic_plugins" / "editor_plugin.cpp";

        // 只生成未保存标签, 不立刻写磁盘。
        upsertEditorDocument(
            source, makeDynamicPluginSourceTemplate(className));
        editorStatus = "动态插件源码已创建（未保存）";
    }

    /**
     * 调用 CMake 编译 MathStudioEditorPlugin, 复制 DLL,
     * 重新扫描动态插件, 并把它挂到当前 Object。
     */
    void compileAndLoadDynamicPlugin(Object& object) {
        const std::filesystem::path root = compileSourceRoot();
        if (root.empty()) {
            editorStatus = "找不到 CMake 源码目录";
            return;
        }

        const std::filesystem::path source =
            root / "src" / "dynamic_plugins" / "editor_plugin.cpp";

        // 新建动态插件可能是“未保存标签”, 此时磁盘上还没有文件。
        // 只要当前标签就是目标源码, 编译前先把它落盘。
        EditorDocument* document = activeEditorDocumentPtr();
        const bool editingSource =
            document != nullptr &&
            document->path.u8string() == source.u8string();

        if (!editingSource && !std::filesystem::exists(source)) {
            editorStatus = "请先创建动态插件源码";
            return;
        }

        // 编辑器中的文本是当前版本, 编译前落盘。
        if (editingSource) {
            std::string error;
            if (!writeTextFile(source, document->buffer.data(), error)) {
                editorStatus = error;
                return;
            }
            document->dirty = false;
        }

        const std::string className =
            sanitizeIdentifier(newPluginClass);
        const std::filesystem::path build =
            compileBuildRoot(root);
        const std::filesystem::path log =
            build / "editor_plugin_build.log";

        const char* cmakeEnvironment = std::getenv("MATHSTUDIO_CMAKE");
        const std::string cmake =
            (cmakeEnvironment != nullptr && cmakeEnvironment[0] != '\0')
                ? std::string(cmakeEnvironment)
                : std::string("C:/Program Files/CMake/bin/cmake.exe");

        const std::string command =
            "cmd.exe /d /s /c \"cd /d " + quotePath(root) +
            " && " + quotePath(std::filesystem::u8path(cmake)) +
            " --build " + quotePath(build) +
            " --config Release --target MathStudioEditorPlugin > " +
            quotePath(log) + " 2>&1\"";

        const int result = std::system(command.c_str());
        if (result != 0) {
            std::string text;
            std::string error;
            if (readTextFile(log, text, error) && !text.empty()) {
                constexpr std::size_t maxTail = 700;
                const std::size_t start =
                    text.size() > maxTail ? text.size() - maxTail : 0;
                editorStatus = "编译失败:\n" + text.substr(start);
            } else {
                editorStatus = "编译失败";
            }
            return;
        }

        const std::filesystem::path dll =
            build / "Release" / "MathStudioEditorPlugin.dll";
        if (!std::filesystem::exists(dll)) {
            editorStatus = "找不到编译产物 DLL";
            return;
        }

        const std::filesystem::path pluginDirectory =
            std::filesystem::current_path() / "plugins";
        std::error_code filesystemError;
        std::filesystem::create_directories(pluginDirectory,
                                            filesystemError);

        const auto stamp = std::chrono::duration_cast<
            std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const std::filesystem::path target =
            pluginDirectory /
            ("EditorPlugin_" + std::to_string(stamp) + ".dll");

        std::filesystem::copy_file(
            dll, target,
            std::filesystem::copy_options::overwrite_existing,
            filesystemError);
        if (filesystemError) {
            editorStatus = "复制 DLL 失败";
            return;
        }

        std::string discoverError;
        DynamicPluginManager::instance().discoverDefault(discoverError);

        std::unique_ptr<Plugin> plugin =
            DynamicPluginManager::instance().create(className);
        if (!plugin) {
            editorStatus = "DLL 已加载, 但类型未注册";
            return;
        }

        object.addPlugin(std::move(plugin));
        markProjectDirty();
        editorStatus = "编译并加载完成";
    }

    /**
     * 把静态插件编译进新的 MathStudio 可执行文件。
     *
     * 当前 MathStudio.exe 正在运行, Windows 无法覆盖它。
     * 因此这里配置一个独立的 build_static 目录, 生成
     * MathStudio_next.exe; 用户重启后使用新程序。
     */
    void compileStaticPlugin() {
        const std::filesystem::path root = compileSourceRoot();
        if (root.empty()) {
            editorStatus = "找不到 CMake 源码目录";
            return;
        }

        const std::string fileBase =
            sanitizeIdentifier(newPluginFile);
        const std::filesystem::path source =
            root / "src" / "plugins" / (fileBase + ".cpp");

        // 先落盘当前编辑内容, 再检查源码是否存在。
        if (!saveAllEditorDocuments()) {
            return;
        }

        if (!std::filesystem::exists(source)) {
            editorStatus = "请先保存静态插件源码";
            return;
        }

        const std::filesystem::path build = root / "build_static";
        const std::filesystem::path log = build / "static_build.log";
        const char* cmakeEnvironment = std::getenv("MATHSTUDIO_CMAKE");
        const std::string cmake =
            (cmakeEnvironment != nullptr && cmakeEnvironment[0] != '\0')
                ? std::string(cmakeEnvironment)
                : std::string("C:/Program Files/CMake/bin/cmake.exe");

        const std::string command =
            "cmd.exe /d /s /c \"cd /d " + quotePath(root) +
            " && " + quotePath(std::filesystem::u8path(cmake)) +
            " -S " + quotePath(root) +
            " -B " + quotePath(build) +
            " -A x64" +
            " && " + quotePath(std::filesystem::u8path(cmake)) +
            " --build " + quotePath(build) +
            " --config Release --target VulkanTutorial > " +
            quotePath(log) + " 2>&1\"";

        const int result = std::system(command.c_str());
        if (result != 0) {
            std::string text;
            std::string error;
            if (readTextFile(log, text, error) && !text.empty()) {
                constexpr std::size_t maxTail = 700;
                const std::size_t start =
                    text.size() > maxTail ? text.size() - maxTail : 0;
                editorStatus = "静态编译失败:\n" + text.substr(start);
            } else {
                editorStatus = "静态编译失败";
            }
            return;
        }

        const std::filesystem::path exe =
            build / "Release" / "VulkanTutorial.exe";
        if (!std::filesystem::exists(exe)) {
            editorStatus = "找不到静态编译产物";
            return;
        }

        const std::filesystem::path working =
            std::filesystem::current_path();
        std::filesystem::path target = working / "MathStudio.exe";
        if (!std::filesystem::exists(target)) {
            target = working / "VulkanTutorial.exe";
        }
        if (!std::filesystem::exists(target)) {
            editorStatus = "找不到当前可执行文件";
            return;
        }

        const std::filesystem::path pending =
            working / "MathStudio_pending.exe";
        std::error_code copyError;
        std::filesystem::copy_file(
            exe, pending,
            std::filesystem::copy_options::overwrite_existing,
            copyError);
        if (copyError) {
            editorStatus = "复制待替换程序失败";
            return;
        }

        // Windows 不能覆盖正在运行的 exe。写一个等待当前进程退出的
        // 批处理: 关闭程序后自动替换并重新启动。
        const std::filesystem::path helper =
            working / "apply_mathstudio_update.bat";
        const std::string targetName =
            target.filename().u8string();
        const std::string batch =
            "@echo off\n"
            "timeout /t 2 /nobreak >nul\n"
            ":wait\n"
            "tasklist /FI \"IMAGENAME eq " + targetName +
            "\" 2>nul | find /I \"" + targetName + "\" >nul\n"
            "if not errorlevel 1 (\n"
            "    timeout /t 1 /nobreak >nul\n"
            "    goto wait\n"
            ")\n"
            "copy /Y " + quotePath(pending) + " " +
            quotePath(target) + " >nul\n"
            "start \"\" " + quotePath(target) + "\n"
            "del \"%~f0\"\n";
        std::string writeError;
        if (!writeTextFile(helper, batch, writeError)) {
            editorStatus = "写入替换脚本失败";
            return;
        }

        const std::string startCommand =
            "start \"\" /b cmd.exe /d /s /c " + quotePath(helper);
        std::system(startCommand.c_str());

        // 让批处理接管: 关闭当前进程, 替换并启动新程序。
        std::exit(0);
    }

    /** 静态插件源码根目录: 优先使用 CMake 工程目录 */
    std::filesystem::path staticSourceRoot() const {
        const std::filesystem::path compiled = compileSourceRoot();
        if (!compiled.empty()) {
            return compiled;
        }
        return findProjectRoot();
    }

    /** 创建静态 C++ 插件 .h/.cpp (不自动覆盖; 调用方先确认) */
    void createStaticPluginSource() {
        const std::string className =
            sanitizeIdentifier(newPluginClass);
        const std::string fileBase =
            sanitizeIdentifier(newPluginFile);
        const std::filesystem::path root = staticSourceRoot();
        if (root.empty()) {
            editorStatus = "找不到项目根目录 (src/plugins)";
            return;
        }

        const std::filesystem::path directory =
            root / "src" / "plugins";
        const std::filesystem::path header =
            directory / (fileBase + ".h");
        const std::filesystem::path source =
            directory / (fileBase + ".cpp");

        // 只生成未保存标签, 不立刻写磁盘。
        upsertEditorDocument(header,
                             makePluginHeaderTemplate(className));
        upsertEditorDocument(
            source,
            makePluginSourceTemplate(className, fileBase + ".h"));
        editorStatus = "静态插件源码已创建（未保存）";
    }

    /** Ctrl+` 打开的插件源码编辑器 */
    void drawPluginEditor() {
        if (!pluginEditorVisible) {
            return;
        }

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowBgAlpha(1.0f);

        // 编辑器可自由拖拽边缘调整大小; 代码区随窗口高度伸缩。
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(620.0f, 420.0f), ImVec2(FLT_MAX, FLT_MAX));
        const ImGuiCond layoutCondition =
            editorLayoutPending ? ImGuiCond_Always
                                : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(
            ImVec2(std::min(1050.0f, display.x - 40.0f),
                   std::min(760.0f, display.y - 40.0f)),
            layoutCondition);
        ImGui::SetNextWindowPos(
            ImVec2(display.x * 0.5f, display.y * 0.5f),
            layoutCondition, ImVec2(0.5f, 0.5f));
        editorLayoutPending = false;

        bool windowOpen = true;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin("插件代码编辑器", &windowOpen, flags)) {
            ImGui::End();
            return;
        }

        // Begin() 的 X 按钮会直接把 windowOpen 置 false。
        // 有未保存文件时拦截关闭, 弹窗确认。
        if (!windowOpen) {
            closeProjectAfterEditorPrompt = false;
            if (hasDirtyEditorDocuments()) {
                editorClosePrompt = true;
                pluginEditorVisible = true;
            } else {
                editorDocuments.clear();
                activeEditorDocument = -1;
                pluginEditorVisible = false;
                ImGui::End();
                return;
            }
        }

        uiSectionTitle("◆", "插件代码");
        uiSectionGap(6.0f);

        // 窗口右上角模式切换。点击后切换静态/动态插件编辑模式。
        ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
        if (ImGui::Button(editorStaticMode ? "静态模式"
                                           : "动态模式",
                          ImVec2(96.0f, 0.0f))) {
            editorStaticMode = !editorStaticMode;
            editorPluginIndex = -1;
            selectVisibleEditorDocument();
        }

        if (!editorStaticMode && owner->objects_.empty()) {
            ImGui::End();
            return;
        }

        Object* object = nullptr;
        if (!owner->objects_.empty()) {
            editorObjectIndex = std::clamp(
                editorObjectIndex, 0,
                static_cast<int>(owner->objects_.size()) - 1);
            object = owner->objects_[static_cast<std::size_t>(
                editorObjectIndex)]
                         .get();
        }

        if (!editorStaticMode && object != nullptr) {
            // ---- 选择 Object ----
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("对象", object->name().c_str())) {
                for (std::size_t i = 0; i < owner->objects_.size(); ++i) {
                    const bool selected =
                        static_cast<int>(i) == editorObjectIndex;
                    if (ImGui::Selectable(
                            owner->objects_[i]->name().c_str(),
                            selected)) {
                        editorObjectIndex = static_cast<int>(i);
                        editorPluginIndex = -1;
                    }
                }
                ImGui::EndCombo();
            }

            // ---- 动态插件列表: 单击插件直接打开对应源码 ----
            uiSectionTitle("◆", "插件");
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(0.0f, 90.0f), ImVec2(FLT_MAX, 160.0f));
            if (ImGui::BeginChild("##editor_plugin_list",
                                  ImVec2(0.0f, 100.0f), true)) {
                const auto& plugins = object->plugins();
                std::size_t dynamicIndex = 0;
                for (const auto& pluginPtr : plugins) {
                    const Plugin& plugin = *pluginPtr;
                    if (!plugin.isDynamic()) {
                        continue;
                    }
                    const bool selected =
                        static_cast<int>(dynamicIndex) ==
                        editorPluginIndex;
                    const std::string label =
                        "     " + plugin.name();
                    ImGui::PushID(
                        static_cast<int>(dynamicIndex));
                    if (ImGui::Selectable(
                            label.c_str(), selected,
                            ImGuiSelectableFlags_None,
                            ImVec2(0.0f, 24.0f))) {
                        editorPluginIndex =
                            static_cast<int>(dynamicIndex);
                        openEditorDocument(
                            sourceFileForPlugin(plugin));
                    }
                    drawPluginKindIcon(
                        plugin, ImGui::GetItemRectMin(),
                        ImGui::GetItemRectMax(), 11.0f);
                    ImGui::PopID();
                    ++dynamicIndex;
                }
            }
            ImGui::EndChild();
        }

        if (editorStaticMode) {
            // ---- 静态插件列表: 只显示已经编译进当前程序的插件 ----
            uiSectionTitle("◆", "静态插件");
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(0.0f, 90.0f), ImVec2(FLT_MAX, 160.0f));
            if (ImGui::BeginChild("##static_plugin_list",
                                  ImVec2(0.0f, 100.0f), true)) {
                std::vector<StaticPluginInfo> infos =
                    staticPluginInfos();

                // 内置静态插件不会经过 plugin_registry (它们由
                // plugin_factory 直接创建), 但同样属于静态插件,
                // 所以合并进列表。
                const std::filesystem::path root =
                    staticSourceRoot();
                const struct BuiltinStatic {
                    const char* typeId;
                    const char* file;
                } builtins[] = {
                    {"ParticlePlugin", "particle_plugin.cpp"},
                    {"SineParticlePlugin", "sine_particle_plugin.cpp"},
                    {"CoordinateSystemPlugin",
                     "coordinate_system_plugin.cpp"},
                    {"ParameterPlugin", "parameter_plugin.cpp"},
                    {"SineMotionPlugin", "sine_motion_plugin.cpp"},
                };
                for (const auto& builtin : builtins) {
                    bool exists = false;
                    for (const auto& info : infos) {
                        if (info.typeId == builtin.typeId) {
                            exists = true;
                            break;
                        }
                    }
                    if (exists) {
                        continue;
                    }
                    StaticPluginInfo info;
                    info.typeId = builtin.typeId;
                    if (!root.empty()) {
                        info.sourcePath =
                            (root / "src" / "plugins" /
                             builtin.file)
                                .u8string();
                    }
                    infos.push_back(std::move(info));
                }

                for (std::size_t i = 0; i < infos.size(); ++i) {
                    const StaticPluginInfo& info = infos[i];
                    const std::string label = "     " + info.typeId;
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(
                            label.c_str(), false,
                            ImGuiSelectableFlags_None,
                            ImVec2(0.0f, 24.0f))) {
                        openEditorDocument(
                            std::filesystem::u8path(
                                info.sourcePath));
                    }
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImVec2 itemMin = ImGui::GetItemRectMin();
                    const ImVec2 itemMax = ImGui::GetItemRectMax();
                    const float centerY =
                        (itemMin.y + itemMax.y) * 0.5f;
                    drawList->AddRectFilled(
                        ImVec2(itemMin.x + 4.0f,
                               centerY - 6.0f),
                        ImVec2(itemMin.x + 16.0f,
                               centerY + 6.0f),
                        IM_COL32(72, 142, 255, 255), 2.0f);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        if (ImGui::Button("↓ 保存")) {
            saveActiveEditorDocument();
        }

        // ---- 多文件标签栏: 只显示当前模式的文件 ----
        selectVisibleEditorDocument();
        if (ImGui::BeginTabBar("##editor_file_tabs",
                                ImGuiTabBarFlags_Reorderable |
                                ImGuiTabBarFlags_AutoSelectNewTabs)) {
            for (std::size_t i = 0; i < editorDocuments.size(); ++i) {
                EditorDocument& document = editorDocuments[i];
                if (!editorDocumentMatchesMode(document.path)) {
                    continue;
                }
                bool tabOpen = true;
                const std::string tabTitle =
                    document.title + (document.dirty ? " *" : "");
                if (ImGui::BeginTabItem(tabTitle.c_str(),
                                        &tabOpen,
                                        ImGuiTabItemFlags_None)) {
                    activeEditorDocument = static_cast<int>(i);
                    ImGui::EndTabItem();
                }

                if (!tabOpen) {
                    if (document.dirty) {
                        pendingCloseTab = static_cast<int>(i);
                        closeTabPrompt = true;
                    } else {
                        pendingCloseTab = static_cast<int>(i);
                    }
                }
            }
            ImGui::EndTabBar();
        }

        // 非脏文件可以直接关闭; 脏文件由弹窗处理。
        if (pendingCloseTab >= 0 && !closeTabPrompt) {
            closeEditorDocument(pendingCloseTab);
            pendingCloseTab = -1;
        }

        // ---- 当前文件代码区 ----
        EditorDocument* document = activeEditorDocumentPtr();
        if (document != nullptr) {
            ImGui::TextDisabled("%s", document->path.u8string().c_str());

            const float available = ImGui::GetContentRegionAvail().y;
            const float editorHeight = std::max(180.0f, available - 205.0f);
            if (ImGui::InputTextMultiline(
                    "##plugin_source", document->buffer.data(),
                    document->buffer.size(), ImVec2(-1.0f, editorHeight),
                    ImGuiInputTextFlags_AllowTabInput)) {
                document->dirty = true;
            }
        }

        // ---- 新建插件 ----
        uiSectionTitle("＋", "新建插件源码");

        ImGui::TextDisabled("类名");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##new_class", newPluginClass,
                         sizeof(newPluginClass));
        ImGui::SameLine();
        ImGui::TextDisabled("文件名");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##new_file", newPluginFile,
                         sizeof(newPluginFile));

        const auto targetExists = [&](PendingCreate action) {
            const std::string className =
                sanitizeIdentifier(newPluginClass);

            if (action == PendingCreate::DynamicPlugin) {
                // editor_plugin.cpp 是固定构建源文件, 它本身总是存在。
                // 因此不能按“文件是否存在”判断, 必须判断是否已有
                // 相同类型名的动态插件 (已加载或当前源码中的类型)。
                const std::vector<std::string> types =
                    DynamicPluginManager::instance().types();
                for (const std::string& type : types) {
                    if (type == className) {
                        return true;
                    }
                }

                const std::filesystem::path root = compileSourceRoot();
                if (root.empty()) {
                    return false;
                }
                const std::filesystem::path source =
                    root / "src" / "dynamic_plugins" /
                    "editor_plugin.cpp";
                std::string text;
                std::string error;
                if (readTextFile(source, text, error)) {
                    return text.find(
                               "kTypeName = \"" + className + "\"") !=
                           std::string::npos;
                }
                return false;
            }

            const std::filesystem::path root = staticSourceRoot();
            if (root.empty()) {
                return false;
            }
            const std::string fileBase =
                sanitizeIdentifier(newPluginFile);
            if (action == PendingCreate::StaticPlugin) {
                return std::filesystem::exists(
                           root / "src" / "plugins" /
                           (fileBase + ".h")) ||
                       std::filesystem::exists(
                           root / "src" / "plugins" /
                           (fileBase + ".cpp"));
            }
            return false;
        };

        const auto requestCreate = [&](PendingCreate action) {
            pendingCreate = action;
            if (targetExists(action)) {
                createPluginPrompt = true;
                return;
            }
            if (action == PendingCreate::StaticPlugin) {
                createStaticPluginSource();
            } else if (action == PendingCreate::DynamicPlugin) {
                createDynamicPluginSource();
            }
        };

        if (editorStaticMode) {
            if (ImGui::Button("＋ 创建静态插件")) {
                requestCreate(PendingCreate::StaticPlugin);
            }

            ImGui::SameLine();
            if (ImGui::Button("编译静态插件")) {
                compileStaticPlugin();
            }

            ImGui::SameLine();
            Object* selectedTarget = nullptr;
            if (selectedObject >= 0 &&
                selectedObject <
                    static_cast<int>(owner->objects_.size())) {
                selectedTarget = owner->objects_[static_cast<std::size_t>(
                    selectedObject)]
                                     .get();
            }
            if (ImGui::Button("→ 应用到当前 Object") &&
                selectedTarget != nullptr) {
                const std::string className =
                    sanitizeIdentifier(newPluginClass);
                std::unique_ptr<Plugin> compiled =
                    createPluginByType(className);
                if (compiled) {
                    selectedTarget->addPlugin(std::move(compiled));
                    markProjectDirty();
                    editorStatus = "插件已应用";
                } else {
                    editorStatus = "当前程序中没有这个静态插件";
                }
            }
        } else {
            if (ImGui::Button("＋ 创建动态插件")) {
                requestCreate(PendingCreate::DynamicPlugin);
            }

            ImGui::SameLine();
            if (ImGui::Button("编译并加载")) {
                if (object != nullptr) {
                    compileAndLoadDynamicPlugin(*object);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("扫描动态插件")) {
                std::string error;
                const std::size_t count =
                    DynamicPluginManager::instance().discoverDefault(
                        error);
                editorStatus =
                    std::to_string(count) + " dynamic plugin(s)";
            }

            ImGui::SameLine();
            if (ImGui::Button("→ 应用到当前 Object") &&
                object != nullptr) {
                const std::string className =
                    sanitizeIdentifier(newPluginClass);
                const std::filesystem::path root = staticSourceRoot();
                const std::string fileBase =
                    sanitizeIdentifier(newPluginFile);
                const std::string sourcePath =
                    root.empty()
                        ? std::string()
                        : (root / "src" / "plugins" /
                           (fileBase + ".cpp"))
                              .string();

                // 先尝试已编译的静态注册插件, 再尝试动态 DLL。
                std::unique_ptr<Plugin> compiled =
                    createPluginByType(className);
                if (compiled) {
                    object->addPlugin(std::move(compiled));
                    markProjectDirty();
                    editorStatus = "插件已应用";
                } else {
                    object->addPlugin<CodePlugin>(
                        className, sourcePath);
                    markProjectDirty();
                    editorStatus =
                        "源码插件已应用（需要编译并注册后生效）";
                }
            }
        }

        // ---- 创建确认: 先保存当前修改, 再覆盖/创建 ----
        if (createPluginPrompt) {
            ImGui::OpenPopup("创建插件");
        }
        if (ImGui::BeginPopupModal("创建插件", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("同名插件源码已存在，继续会覆盖。");
            if (hasDirtyEditorDocuments()) {
                ImGui::TextUnformatted("当前文件有未保存修改。");
            }

            if (ImGui::Button("保存并创建", ImVec2(130.0f, 32.0f))) {
                if (saveAllEditorDocuments()) {
                    const PendingCreate action = pendingCreate;
                    pendingCreate = PendingCreate::None;
                    createPluginPrompt = false;
                    if (action == PendingCreate::StaticPlugin) {
                        createStaticPluginSource();
                    } else if (action == PendingCreate::DynamicPlugin) {
                        createDynamicPluginSource();
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("不保存并创建", ImVec2(140.0f, 32.0f))) {
                const PendingCreate action = pendingCreate;
                pendingCreate = PendingCreate::None;
                createPluginPrompt = false;
                if (action == PendingCreate::StaticPlugin) {
                    createStaticPluginSource();
                } else if (action == PendingCreate::DynamicPlugin) {
                    createDynamicPluginSource();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 32.0f))) {
                pendingCreate = PendingCreate::None;
                createPluginPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- 关闭编辑器时的未保存确认 ----
        if (editorClosePrompt) {
            ImGui::OpenPopup("未保存的源码");
        }
        if (ImGui::BeginPopupModal("未保存的源码", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("是否保存所有修改？");

            if (ImGui::Button("保存全部", ImVec2(110.0f, 32.0f))) {
                if (saveAllEditorDocuments()) {
                    editorDocuments.clear();
                    activeEditorDocument = -1;
                    editorClosePrompt = false;
                    pluginEditorVisible = false;
                    if (closeProjectAfterEditorPrompt) {
                        closeProjectAfterEditorPrompt = false;
                        if (owner->menu() != nullptr &&
                            owner->menu()->isProjectDirty()) {
                            projectClosePrompt = true;
                        } else {
                            hide();
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("不保存", ImVec2(100.0f, 32.0f))) {
                editorDocuments.clear();
                activeEditorDocument = -1;
                editorClosePrompt = false;
                pluginEditorVisible = false;
                if (closeProjectAfterEditorPrompt) {
                    closeProjectAfterEditorPrompt = false;
                    if (owner->menu() != nullptr &&
                        owner->menu()->isProjectDirty()) {
                        projectClosePrompt = true;
                    } else {
                        hide();
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 32.0f))) {
                editorClosePrompt = false;
                closeProjectAfterEditorPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- 关闭单个标签时的未保存确认 ----
        if (closeTabPrompt) {
            ImGui::OpenPopup("关闭文件");
        }
        if (ImGui::BeginPopupModal("关闭文件", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("当前文件有未保存修改。");

            if (ImGui::Button("保存并关闭", ImVec2(120.0f, 32.0f))) {
                if (pendingCloseTab >= 0 &&
                    saveEditorDocument(pendingCloseTab)) {
                    closeEditorDocument(pendingCloseTab);
                    pendingCloseTab = -1;
                    closeTabPrompt = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("不保存", ImVec2(100.0f, 32.0f))) {
                if (pendingCloseTab >= 0) {
                    closeEditorDocument(pendingCloseTab);
                    pendingCloseTab = -1;
                }
                closeTabPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 32.0f))) {
                pendingCloseTab = -1;
                closeTabPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (!editorStatus.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", editorStatus.c_str());
        }

        ImGui::End();
    }

    /** 把设置中的快捷键同步到编辑缓冲 */
    void refreshSettingsBuffers() {
        const AppSettings& settings = appSettings();
        const std::array<const std::string*, 8> values = {
            &settings.saveShortcut,
            &settings.saveAsShortcut,
            &settings.openShortcut,
            &settings.newShortcut,
            &settings.closeShortcut,
            &settings.settingsShortcut,
            &settings.editorShortcut,
            &settings.timelineShortcut,
        };

        for (std::size_t i = 0; i < values.size(); ++i) {
            std::snprintf(shortcutBuffers[i].data(),
                          shortcutBuffers[i].size(), "%s",
                          values[i]->c_str());
        }
        settingsBuffersReady = true;
    }

    /** 在原点直接创建一个内置基本体 */
    void addPrimitive(const char* meshFile, const char* name,
                      const glm::vec4& color) {
        try {
            Mesh mesh = Mesh::loadFromResource(meshFile);
            owner->addObject<Object>(std::move(mesh), glm::vec3(0.0f),
                                     color, name);
            selectedObject =
                static_cast<int>(owner->objectCount()) - 1;
            hasSelection = true;
            panelScrollToTop = true;
        } catch (const std::exception& error) {
            importStatus = error.what();
        }
    }

    /** 菜单 Object > 坐标系 */
    void addCoordinateSystemObject() {
        try {
            Mesh mesh = Mesh::loadFromResource("cube.mesh");
            Object& object = owner->addObject<Object>(
                std::move(mesh), glm::vec3(0.0f),
                glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), "坐标系");
            object.addPlugin<CoordinateSystemPlugin>(2.0f, true);

            selectedObject =
                static_cast<int>(owner->objectCount()) - 1;
            hasSelection = true;
            panelScrollToTop = true;
        } catch (const std::exception& error) {
            importStatus = error.what();
        }
    }

    /** 菜单 Object > 正弦运动立方体 (插件测试) */
    void addSineMotionCube() {
        try {
            Mesh mesh = Mesh::loadFromResource("cube.mesh");
            Object& object = owner->addObject<Object>(
                std::move(mesh), glm::vec3(0.0f),
                glm::vec4(0.76f, 0.84f, 1.0f, 1.0f), "正弦运动立方体");
            object.addPlugin<SineMotionPlugin>(0.8f, 0.35f,
                                               3.0f, 1.4f, 0.0f);

            selectedObject =
                static_cast<int>(owner->objectCount()) - 1;
            hasSelection = true;
            panelScrollToTop = true;
        } catch (const std::exception& error) {
            importStatus = error.what();
        }
    }

    /** 菜单 Object > 动态实体演示 */
    void addEntityDemoCube() {
        try {
            std::unique_ptr<Plugin> plugin =
                DynamicPluginManager::instance().create(
                    "EntityDemoPlugin");
            if (!plugin) {
                importStatus = "EntityDemoPlugin.dll not loaded";
                return;
            }

            Mesh mesh = Mesh::loadFromResource("cube.mesh");
            Object& object = owner->addObject<Object>(
                std::move(mesh), glm::vec3(0.0f),
                glm::vec4(0.72f, 0.90f, 0.95f, 1.0f),
                "动态实体演示");
            object.addPlugin(std::move(plugin));

            selectedObject =
                static_cast<int>(owner->objectCount()) - 1;
            hasSelection = true;
            panelScrollToTop = true;
        } catch (const std::exception& error) {
            importStatus = error.what();
        }
    }

    /** 菜单 Object > 动态波浪立方体 */
    void addDynamicWaveCube() {
        try {
            std::unique_ptr<Plugin> plugin =
                DynamicPluginManager::instance().create(
                    "WaveMotionPlugin");
            if (!plugin) {
                importStatus = "WaveMotionPlugin.dll not loaded";
                return;
            }

            Mesh mesh = Mesh::loadFromResource("cube.mesh");
            Object& object = owner->addObject<Object>(
                std::move(mesh), glm::vec3(0.0f),
                glm::vec4(0.72f, 0.90f, 0.95f, 1.0f), "动态波浪立方体");
            object.addPlugin(std::move(plugin));

            selectedObject =
                static_cast<int>(owner->objectCount()) - 1;
            hasSelection = true;
            panelScrollToTop = true;
        } catch (const std::exception& error) {
            importStatus = error.what();
        }
    }

    /** 工程窗口顶部 File / Setting 菜单栏 */
    void drawMenuBar() {
        Menu* projectMenu = owner->menu();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                const AppSettings& settings = appSettings();

                if (ImGui::MenuItem("新建工程",
                                    settings.newShortcut.c_str()) &&
                    projectMenu != nullptr) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::New);
                }
                if (ImGui::MenuItem("打开工程...",
                                    settings.openShortcut.c_str()) &&
                    projectMenu != nullptr) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::Open);
                }
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                if (ImGui::MenuItem("保存",
                                    settings.saveShortcut.c_str()) &&
                    projectMenu != nullptr) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::Save);
                }
                if (ImGui::MenuItem("另存为...",
                                    settings.saveAsShortcut.c_str()) &&
                    projectMenu != nullptr) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::SaveAs);
                }
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                if (ImGui::MenuItem("关闭工程",
                                    settings.closeShortcut.c_str()) &&
                    projectMenu != nullptr) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::Close);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Object")) {
                if (ImGui::MenuItem("坐标系")) {
                    addCoordinateSystemObject();
                }
                if (ImGui::MenuItem("正方体")) {
                    addPrimitive("cube.mesh", "正方体",
                                 glm::vec4(0.72f, 0.80f, 1.0f, 1.0f));
                }
                if (ImGui::MenuItem("圆柱体")) {
                    addPrimitive("cylinder.mesh", "圆柱体",
                                 glm::vec4(0.60f, 0.90f, 0.72f, 1.0f));
                }
                if (ImGui::MenuItem("圆锥体")) {
                    addPrimitive("cone.mesh", "圆锥体",
                                 glm::vec4(1.0f, 0.72f, 0.38f, 1.0f));
                }
                if (ImGui::MenuItem("球体")) {
                    addPrimitive("sphere.mesh", "球体",
                                 glm::vec4(0.78f, 0.62f, 1.0f, 1.0f));
                }
                if (ImGui::MenuItem("平面")) {
                    addPrimitive("plane.mesh", "平面",
                                 glm::vec4(0.62f, 0.68f, 0.72f, 1.0f));
                }
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                if (ImGui::MenuItem("正弦运动立方体")) {
                    addSineMotionCube();
                }
                if (ImGui::MenuItem("动态波浪立方体")) {
                    addDynamicWaveCube();
                }
                if (ImGui::MenuItem("动态实体演示")) {
                    addEntityDemoCube();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Setting")) {
                if (ImGui::MenuItem("设置...",
                                    appSettings().settingsShortcut.c_str())) {
                    settingsVisible = true;
                    settingsScrollToTop = true;
                }
                ImGui::EndMenu();
            }

            menuBarHeight = ImGui::GetWindowHeight();
            ImGui::EndMainMenuBar();
        }

        if (menuBarHeight <= 0.0f) {
            menuBarHeight = ImGui::GetFrameHeight();
        }
    }

    /** 设置窗口: 快捷键 + UI 细节 + 导入配置 */
    void drawSettings() {
        if (!settingsVisible) {
            return;
        }
        if (!settingsBuffersReady) {
            refreshSettingsBuffers();
        }

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(
            ImVec2(600.0f, std::min(760.0f, display.y - 40.0f)),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                                ImGuiCond_FirstUseEver,
                                ImVec2(0.5f, 0.5f));

        if (!ImGui::Begin("设置", &settingsVisible,
                          ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        if (settingsScrollToTop) {
            ImGui::SetScrollY(0.0f);
            settingsScrollToTop = false;
        }

        AppSettings& settings = appSettings();

        uiSectionTitle("◆", "快捷键");

        const auto shortcutRow = [&](const char* label,
                                     std::array<char, 64>& buffer,
                                     std::string& value) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine(150.0f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText((std::string("##") + label).c_str(),
                                 buffer.data(), buffer.size())) {
                value = buffer.data();
                markUiSettingsChanged();
            }
        };

        shortcutRow("保存", shortcutBuffers[0], settings.saveShortcut);
        shortcutRow("另存为", shortcutBuffers[1],
                    settings.saveAsShortcut);
        shortcutRow("打开", shortcutBuffers[2], settings.openShortcut);
        shortcutRow("新建", shortcutBuffers[3], settings.newShortcut);
        shortcutRow("关闭工程", shortcutBuffers[4],
                    settings.closeShortcut);
        shortcutRow("打开设置", shortcutBuffers[5],
                    settings.settingsShortcut);
        shortcutRow("插件编辑器", shortcutBuffers[6],
                    settings.editorShortcut);
        shortcutRow("时间轴", shortcutBuffers[7],
                    settings.timelineShortcut);

        uiSectionGap(10.0f);
        uiSectionTitle("◆", "界面");

        float accent[3] = {settings.accent.x, settings.accent.y,
                           settings.accent.z};
        if (ImGui::ColorEdit3("强调色", accent,
                              ImGuiColorEditFlags_NoInputs)) {
            settings.accent = ImVec4(accent[0], accent[1], accent[2], 1.0f);
            markUiSettingsChanged();
        }

        float windowBg[3] = {settings.windowBg.x, settings.windowBg.y,
                             settings.windowBg.z};
        if (ImGui::ColorEdit3("窗口背景", windowBg,
                              ImGuiColorEditFlags_NoInputs)) {
            settings.windowBg =
                ImVec4(windowBg[0], windowBg[1], windowBg[2], 1.0f);
            markUiSettingsChanged();
        }

        if (ImGui::SliderFloat("面板透明度", &settings.panelOpacity,
                               0.2f, 1.0f, "%.2f")) {
            markUiSettingsChanged();
        }
        if (ImGui::SliderFloat("窗口圆角", &settings.windowRounding,
                               0.0f, 24.0f, "%.0f")) {
            markUiSettingsChanged();
        }
        if (ImGui::SliderFloat("控件圆角", &settings.frameRounding,
                               0.0f, 24.0f, "%.0f")) {
            markUiSettingsChanged();
        }
        if (ImGui::SliderFloat("纵向间距", &settings.itemSpacingY,
                               0.0f, 30.0f, "%.0f")) {
            markUiSettingsChanged();
        }
        if (ImGui::SliderFloat("窗口内边距", &settings.windowPadding,
                               4.0f, 40.0f, "%.0f")) {
            markUiSettingsChanged();
        }
        if (ImGui::SliderFloat("界面缩放", &settings.uiScale,
                               0.75f, 1.75f, "%.2f")) {
            markUiSettingsChanged();
        }
        if (ImGui::Checkbox("显示图标", &settings.showIcons)) {
            markUiSettingsChanged();
        }

        uiSectionGap(10.0f);

        if (ImGui::Button("导入配置...")) {
            const std::string path = openSettingsFileDialog();
            if (!path.empty()) {
                std::string error;
                if (loadSettingsFile(path, error)) {
                    settingsStatus = path;
                    refreshSettingsBuffers();
                } else {
                    settingsStatus = error;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("保存配置")) {
            std::string error;
            if (saveSettingsFile(defaultSettingsPath().u8string(), error)) {
                settingsStatus = defaultSettingsPath().u8string();
            } else {
                settingsStatus = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("恢复默认")) {
            resetAppSettings();
            refreshSettingsBuffers();
            settingsStatus.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("关闭")) {
            settingsVisible = false;
        }

        if (!settingsStatus.empty()) {
            ImGui::TextWrapped("%s", settingsStatus.c_str());
        }

        ImGui::End();
    }

    /** 工程有未保存修改时, 关闭工程窗口前的确认 */
    void drawProjectClosePrompt() {
        if (!projectClosePrompt) {
            return;
        }

        ImGui::OpenPopup("保存工程");
        if (ImGui::BeginPopupModal("保存工程", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("工程有未保存修改。");

            if (ImGui::Button("保存", ImVec2(100.0f, 32.0f))) {
                Menu* projectMenu = owner->menu();
                if (projectMenu != nullptr) {
                    std::string path = projectMenu->projectPath();
                    if (path.empty()) {
                        path = saveMathFileDialog(projectMenu->projectName());
                    }
                    if (!path.empty()) {
                        std::string error;
                        if (projectMenu->saveProject(path, error)) {
                            projectClosePrompt = false;
                            hide();
                            ImGui::CloseCurrentPopup();
                        } else {
                            editorStatus = error;
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("不保存", ImVec2(100.0f, 32.0f))) {
                if (owner->menu() != nullptr) {
                    owner->menu()->clearProjectDirty();
                }
                projectClosePrompt = false;
                hide();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(90.0f, 32.0f))) {
                projectClosePrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    /** 正交视图右上角的方向指示: 白方块 + 透明圆环 + 大字 */
    void drawViewIndicator() {
        if (viewIndicator.empty() || !camera.orthographic) {
            return;
        }

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        constexpr float ringRadius = 46.0f;
        constexpr float squareHalf = 32.0f;
        constexpr float margin = 18.0f;

        const ImVec2 center(
            display.x - margin - ringRadius,
            menuBarHeight + margin + ringRadius);

        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        // 透明圆环。
        drawList->AddCircle(center, ringRadius,
                            IM_COL32(255, 255, 255, 165), 64, 2.0f);
        drawList->AddCircle(center, ringRadius - 3.0f,
                            IM_COL32(255, 255, 255, 70), 64, 1.0f);

        // 圆环正中心的白色正方形。
        const ImVec2 squareMin(center.x - squareHalf,
                               center.y - squareHalf);
        const ImVec2 squareMax(center.x + squareHalf,
                               center.y + squareHalf);
        drawList->AddRectFilled(squareMin, squareMax,
                                IM_COL32(255, 255, 255, 242), 5.0f);

        // 透明、加粗、大号的方向字。
        ImFont* font = ImGui::GetFont();
        constexpr float fontSize = 40.0f;
        const ImVec2 textSize = font->CalcTextSizeA(
            fontSize, FLT_MAX, 0.0f, viewIndicator.c_str());
        const ImVec2 textPos(
            center.x - textSize.x * 0.5f,
            center.y - textSize.y * 0.5f);
        const ImU32 textColor = IM_COL32(24, 24, 28, 150);

        // ImGui 字体没有粗体, 通过多次偏移绘制模拟加粗。
        const ImVec2 offsets[] = {
            ImVec2(-1.5f, 0.0f), ImVec2(1.5f, 0.0f),
            ImVec2(0.0f, -1.5f), ImVec2(0.0f, 1.5f),
            ImVec2(1.0f, 1.0f), ImVec2(-1.0f, 1.0f),
            ImVec2(1.0f, -1.0f), ImVec2(-1.0f, -1.0f),
        };
        for (const ImVec2& offset : offsets) {
            drawList->AddText(font, fontSize,
                              ImVec2(textPos.x + offset.x,
                                     textPos.y + offset.y),
                              textColor, viewIndicator.c_str());
        }
    }

    /** 绘制可收起的参数浮窗 */
    void drawFloatingPanel() {
        const auto objectCount = owner->objects_.size();
        if (objectCount == 0) {
            selectedObject = 0;
        } else {
            selectedObject = std::clamp(
                selectedObject, 0, static_cast<int>(objectCount) - 1);
        }

        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const float topOffset = menuBarHeight;
        const float sceneAvailableHeight =
            std::max(1.0f, displaySize.y - topOffset);

        // ---- 收起状态: 只显示一个箭头按钮 ----
        if (panelCollapsed) {
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth,
                                   &framebufferHeight);
            const float scaleY = displaySize.y > 0.0f
                                     ? static_cast<float>(framebufferHeight) /
                                           displaySize.y
                                     : 1.0f;

            scenePos = ImVec2(0.0f, topOffset);
            sceneSize = ImVec2(displaySize.x, sceneAvailableHeight);
            sceneViewport.offset = {
                0, static_cast<int32_t>(topOffset * scaleY)};
            sceneViewport.extent = {
                static_cast<uint32_t>(std::max(1, framebufferWidth)),
                static_cast<uint32_t>(std::max(
                    1.0f, sceneAvailableHeight * scaleY))};

            ImGui::SetNextWindowPos(
                ImVec2(panelPos.x, std::max(panelPos.y, topOffset + 8.0f)),
                ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(appSettings().panelOpacity);

            const ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoScrollbar;

            ImGui::Begin("##collapsed_parameters", nullptr, flags);

            // ImGui 自带的三角形箭头按钮, 不依赖字体里的箭头字符。
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(10.0f, 10.0f));
            const bool expand =
                ImGui::ArrowButton("##expand", ImGuiDir_Right);
            ImGui::PopStyleVar();

            if (expand) {
                panelCollapsed = false;
            }

            ImGui::End();
            return;
        }

        // ---- 计算停靠布局 ----
        ImVec2 panelPosForFrame = panelPos;
        ImVec2 panelSizeForFrame = panelSize;
        bool docked = (dockMode != PanelDock::Floating);

        scenePos = ImVec2(0.0f, topOffset);
        sceneSize = ImVec2(displaySize.x, sceneAvailableHeight);

        if (docked) {
            const float sideWidth = std::clamp(
                panelSize.x, 260.0f, displaySize.x * 0.60f);
            const float sideHeight = std::clamp(
                panelSize.y, 220.0f, sceneAvailableHeight * 0.60f);

            switch (dockMode) {
            case PanelDock::Left:
                panelPosForFrame = ImVec2(0.0f, topOffset);
                panelSizeForFrame = ImVec2(sideWidth, sceneAvailableHeight);
                scenePos = ImVec2(sideWidth, topOffset);
                sceneSize = ImVec2(displaySize.x - sideWidth,
                                   sceneAvailableHeight);
                break;
            case PanelDock::Right:
                panelPosForFrame =
                    ImVec2(displaySize.x - sideWidth, topOffset);
                panelSizeForFrame = ImVec2(sideWidth, sceneAvailableHeight);
                scenePos = ImVec2(0.0f, topOffset);
                sceneSize = ImVec2(displaySize.x - sideWidth,
                                   sceneAvailableHeight);
                break;
            case PanelDock::Top:
                panelPosForFrame = ImVec2(0.0f, topOffset);
                panelSizeForFrame =
                    ImVec2(displaySize.x, sideHeight);
                scenePos = ImVec2(0.0f, topOffset + sideHeight);
                sceneSize = ImVec2(displaySize.x,
                                   sceneAvailableHeight - sideHeight);
                break;
            case PanelDock::Bottom:
                panelPosForFrame =
                    ImVec2(0.0f, displaySize.y - sideHeight);
                panelSizeForFrame =
                    ImVec2(displaySize.x, sideHeight);
                scenePos = ImVec2(0.0f, topOffset);
                sceneSize = ImVec2(displaySize.x,
                                   sceneAvailableHeight - sideHeight);
                break;
            case PanelDock::Floating:
                break;
            }
        } else {
            // 浮动窗口不允许盖住顶部菜单栏。
            panelPosForFrame.y =
                std::max(panelPosForFrame.y, topOffset + 8.0f);
        }

        // ImGui 使用逻辑坐标, Vulkan 使用 framebuffer 像素; 做一次缩放。
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth,
                               &framebufferHeight);
        const float scaleX = displaySize.x > 0.0f
                                 ? static_cast<float>(framebufferWidth) /
                                       displaySize.x
                                 : 1.0f;
        const float scaleY = displaySize.y > 0.0f
                                 ? static_cast<float>(framebufferHeight) /
                                       displaySize.y
                                 : 1.0f;

        sceneViewport.offset = {
            static_cast<int32_t>(scenePos.x * scaleX),
            static_cast<int32_t>(scenePos.y * scaleY)};
        sceneViewport.extent = {
            static_cast<uint32_t>(std::max(
                1.0f, sceneSize.x * scaleX)),
            static_cast<uint32_t>(std::max(
                1.0f, sceneSize.y * scaleY))};

        // ---- 展开状态: 完整参数浮窗 ----
        const ImGuiCond positionCondition =
            (docked || forceFloatingPosition)
                ? ImGuiCond_Always
                : ImGuiCond_Appearing;

        ImGui::SetNextWindowPos(panelPosForFrame, positionCondition);

        // 只有刚切换停靠状态时强制设置尺寸; 其余时候允许用户拖动
        // 浮窗内边缘改变宽度/高度。
        const ImGuiCond sizeCondition =
            dockLayoutPending ? ImGuiCond_Always : ImGuiCond_Appearing;
        ImGui::SetNextWindowSize(panelSizeForFrame, sizeCondition);
        dockLayoutPending = false;
        forceFloatingPosition = false;
        ImGui::SetNextWindowBgAlpha(appSettings().panelOpacity);

        const std::string title =
            owner->title_ + " 参数###object_parameters";

        // 停靠后不再禁止 Resize, 这样用户可以拖动内边缘调整宽度。
        const ImGuiWindowFlags panelFlags =
            ImGuiWindowFlags_NoCollapse |
            (docked ? ImGuiWindowFlags_NoMove : 0);

        ImGui::Begin(title.c_str(), nullptr, panelFlags);

        ImGui::TextUnformatted(owner->title_.c_str());

        // 右上角的收起按钮: 记住当前位置和大小, 收起后箭头停在这里。
        ImGui::SameLine(ImGui::GetWindowWidth() - 50.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(8.0f, 8.0f));
        const bool collapse =
            ImGui::ArrowButton("##collapse", ImGuiDir_Down);
        ImGui::PopStyleVar();

        if (collapse) {
            panelPos = ImGui::GetWindowPos();
            panelSize = ImGui::GetWindowSize();
            panelCollapsed = true;
            ImGui::End();
            return;
        }

        uiSectionGap(4.0f);

        // 标题栏空白区域: 拖动它选择停靠方向。
        // 不占用内容区域, 也不额外绘制按钮。
        const ImVec2 titleWindowPos = ImGui::GetWindowPos();
        const ImVec2 titleWindowSize = ImGui::GetWindowSize();
        const float titleHeight =
            ImGui::GetFontSize() +
            ImGui::GetStyle().FramePadding.y * 2.0f;
        const std::string visibleTitle =
            owner->title_ + " 参数";
        const float titleTextWidth =
            ImGui::CalcTextSize(visibleTitle.c_str()).x;

        const float blankLeft = titleWindowPos.x +
                                ImGui::GetStyle().WindowPadding.x +
                                titleTextWidth + 8.0f;
        // 右侧留出收起按钮的位置。
        const float blankRight = titleWindowPos.x +
                                 titleWindowSize.x - 58.0f;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool mouseInTitleBlank =
            mouse.x >= blankLeft && mouse.x <= blankRight &&
            mouse.y >= titleWindowPos.y &&
            mouse.y <= titleWindowPos.y + titleHeight;

        if (ImGui::IsMouseClicked(0) && mouseInTitleBlank) {
            dockDragArmed = true;
        }

        if (dockDragArmed && ImGui::IsMouseDragging(0, 4.0f)) {
            dockDragging = true;
        }

        if (dockDragging) {
            dockPreview = pickDockFromMouse(mouse);
            drawDockPreview(dockPreview);

            if (ImGui::IsMouseReleased(0)) {
                applyDockTarget(dockPreview, mouse);
                dockDragging = false;
                dockDragArmed = false;
            }
        } else if (!ImGui::IsMouseDown(0)) {
            dockDragArmed = false;
        }

        uiSectionTitle("◆", "时间轴");

        // 时间轴开关放在顶部, 给下方的层级列表和参数区留空间。
        if (ImGui::Button(timelineVisible ? "□ 时间轴 (T) 开"
                                          : "□ 时间轴 (T) 关")) {
            timelineVisible = !timelineVisible;
        }
        ImGui::SameLine();
        if (ImGui::Button(timelinePaused ? "→ 继续" : "Ⅱ 暂停")) {
            timelinePaused = !timelinePaused;
        }

        uiSectionGap(8.0f);

        // 点击 3D 物体切换对象后, 把浮窗滚回顶部。
        if (panelScrollToTop) {
            ImGui::SetScrollY(0.0f);
            panelScrollToTop = false;
        }

        // ---- 上半区: 组别 + Object 列表 ----
        const float availableHeight =
            ImGui::GetContentRegionAvail().y;
        const float hierarchyHeight =
            std::max(140.0f, availableHeight * 0.45f);
        drawHierarchy(hierarchyHeight);

        uiSectionGap(8.0f);

        // ---- 下半区: 当前选中 Object 的参数 ----
        ImGui::BeginChild("##object_parameters", ImVec2(0, 0), true);
        uiSectionTitle("◆", "对象参数");

        if (owner->objects_.empty()) {
        } else {
            selectedObject = std::clamp(
                selectedObject, 0,
                static_cast<int>(owner->objects_.size()) - 1);
            Object& selected =
                *owner->objects_[static_cast<std::size_t>(selectedObject)];

            // 名称也可以直接在参数区改。
            char nameBuffer[128] = {};
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s",
                          selected.name().c_str());
            if (ImGui::InputText("名称", nameBuffer,
                                 sizeof(nameBuffer))) {
                selected.setName(nameBuffer);
                markProjectDirty();
            }

            drawObjectSection(selected);
            drawPluginSection(selected);
        }

        ImGui::EndChild();

        // ---- 记录当前浮窗位置/大小 ----
        // 刚通过拖动改变停靠方向时, 下一帧才会使用新的布局,
        // 这一帧不要用旧的窗口尺寸覆盖目标状态。
        if (!dockLayoutPending) {
            const ImVec2 currentPanelPos = ImGui::GetWindowPos();
            const ImVec2 currentPanelSize = ImGui::GetWindowSize();

            if (dockMode == PanelDock::Floating) {
                panelPos = currentPanelPos;
                panelSize = currentPanelSize;
            } else if (dockMode == PanelDock::Left ||
                       dockMode == PanelDock::Right) {
                // 左右停靠: 用户拖动内边缘改变宽度。
                panelSize.x = currentPanelSize.x;
            } else {
                // 顶部/底部停靠: 用户拖动内边缘改变高度。
                panelSize.y = currentPanelSize.y;
            }
        }

        ImGui::End();
    }

    // ----------------------------------------------------------------
    // ImGui 输入转发
    // ----------------------------------------------------------------

    /** GLFW 键码 -> ImGuiKey */
    static ImGuiKey toImGuiKey(int key) {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            return static_cast<ImGuiKey>(
                ImGuiKey_A + (key - GLFW_KEY_A));
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
            return static_cast<ImGuiKey>(
                ImGuiKey_0 + (key - GLFW_KEY_0));
        }

        switch (key) {
        case GLFW_KEY_TAB: return ImGuiKey_Tab;
        case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
        case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
        case GLFW_KEY_UP: return ImGuiKey_UpArrow;
        case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
        case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
        case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
        case GLFW_KEY_HOME: return ImGuiKey_Home;
        case GLFW_KEY_END: return ImGuiKey_End;
        case GLFW_KEY_INSERT: return ImGuiKey_Insert;
        case GLFW_KEY_DELETE: return ImGuiKey_Delete;
        case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
        case GLFW_KEY_SPACE: return ImGuiKey_Space;
        case GLFW_KEY_ENTER: return ImGuiKey_Enter;
        case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
        case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
        case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
        case GLFW_KEY_COMMA: return ImGuiKey_Comma;
        case GLFW_KEY_MINUS: return ImGuiKey_Minus;
        case GLFW_KEY_PERIOD: return ImGuiKey_Period;
        case GLFW_KEY_SLASH: return ImGuiKey_Slash;
        case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
        case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
        case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
        case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
        case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
        case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
        default: return ImGuiKey_None;
        }
    }

    /** 把 GLFW 键盘事件转发给子窗口自己的 ImGui 上下文 */
    void feedKey(int key, int action, int mods) {
        if (imguiContext == nullptr) {
            return;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);

        ImGuiIO& io = ImGui::GetIO();
        const ImGuiKey imguiKey = toImGuiKey(key);
        if (imguiKey != ImGuiKey_None) {
            io.AddKeyEvent(imguiKey, action != GLFW_RELEASE);
        }

        io.AddKeyEvent(ImGuiMod_Ctrl,
                       (mods & GLFW_MOD_CONTROL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift,
                       (mods & GLFW_MOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt,
                       (mods & GLFW_MOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super,
                       (mods & GLFW_MOD_SUPER) != 0);

        ImGui::SetCurrentContext(previous);
    }

    /** 把字符事件转发给子窗口自己的 ImGui 上下文 (InputText 使用) */
    void feedChar(unsigned int codepoint) {
        if (imguiContext == nullptr) {
            return;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        ImGui::GetIO().AddInputCharacter(codepoint);
        ImGui::SetCurrentContext(previous);
    }

    /** 把鼠标位置转发给子窗口自己的 ImGui 上下文 */
    void feedMousePos(double x, double y) {
        if (imguiContext == nullptr) {
            return;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(x),
                                        static_cast<float>(y));
        ImGui::SetCurrentContext(previous);
    }

    /** 把鼠标按键转发给子窗口自己的 ImGui 上下文 */
    void feedMouseButton(int button, int action) {
        if (imguiContext == nullptr) {
            return;
        }

        int imguiButton = -1;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            imguiButton = 0;
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            imguiButton = 1;
        } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
            imguiButton = 2;
        }

        if (imguiButton < 0) {
            return;
        }

        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);

        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(static_cast<float>(x),
                            static_cast<float>(y));
        io.AddMouseButtonEvent(imguiButton, action != GLFW_RELEASE);

        ImGui::SetCurrentContext(previous);
    }

    /** 把滚轮事件转发给子窗口自己的 ImGui 上下文 */
    void feedScroll(double yOffset) {
        if (imguiContext == nullptr) {
            return;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        ImGui::GetIO().AddMouseWheelEvent(0.0f,
                                          static_cast<float>(yOffset));
        ImGui::SetCurrentContext(previous);
    }

    /** 把窗口焦点转发给子窗口自己的 ImGui 上下文 */
    void feedFocus(bool focused) {
        if (imguiContext == nullptr) {
            return;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        ImGui::GetIO().AddFocusEvent(focused);
        ImGui::SetCurrentContext(previous);
    }

    /** 当前鼠标是否被浮窗捕获 (是则不要旋转/平移/缩放相机) */
    bool imguiWantsMouse() const {
        if (imguiContext == nullptr || !imguiBackendReady) {
            return false;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        const bool wanted = ImGui::GetIO().WantCaptureMouse;
        ImGui::SetCurrentContext(previous);
        return wanted;
    }

    /** 当前键盘是否被浮窗/时间轴捕获 (用于避免 T 键误切换) */
    bool imguiWantsKeyboard() const {
        if (imguiContext == nullptr || !imguiBackendReady) {
            return false;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        const bool wanted = ImGui::GetIO().WantCaptureKeyboard;
        ImGui::SetCurrentContext(previous);
        return wanted;
    }

    /** 当前是否有输入框正在接收文字 (T 键不应切换时间轴) */
    bool imguiWantsTextInput() const {
        if (imguiContext == nullptr || !imguiBackendReady) {
            return false;
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);
        const bool wanted = ImGui::GetIO().WantTextInput;
        ImGui::SetCurrentContext(previous);
        return wanted;
    }

    // ================================================================
    // 时间轴
    // ================================================================

    /**
     * 推进时间轴, 并让所有对象定位到当前时间
     *
     * 暂停时 timelineTime 不变, 但仍然调用 setAnimationTime,
     * 这样拖动进度条时画面可以立即更新。
     */
    void advanceTimeline(float deltaSeconds) {
        if (!timelinePaused) {
            timelineTime += deltaSeconds;
            if (timelineTime > timelineDuration) {
                timelineTime = std::fmod(timelineTime, timelineDuration);
            }
        }

        for (const auto& object : owner->objects_) {
            object->setAnimationTime(timelineTime);
        }
    }

    /** 子窗口底部的时间轴浮条 */
    void drawTimeline() {
        if (!timelineVisible) {
            return;
        }

        constexpr float barHeight = 76.0f;

        // 时间轴贴在 3D 场景区域底部; 浮窗停靠时不会覆盖浮窗。
        ImGui::SetNextWindowPos(ImVec2(
            scenePos.x,
            scenePos.y + sceneSize.y - barHeight));
        ImGui::SetNextWindowSize(
            ImVec2(sceneSize.x, barHeight));
        ImGui::SetNextWindowBgAlpha(appSettings().panelOpacity);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("##subwindow_timeline", nullptr, flags);

        if (ImGui::Button(timelinePaused ? "→ 播放" : "Ⅱ 暂停",
                          ImVec2(92.0f, 30.0f))) {
            timelinePaused = !timelinePaused;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%.2f / %.2f s",
                            timelineTime, timelineDuration);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##timeline_slider", &timelineTime,
                           0.0f, timelineDuration, "%.2f s");

        // 拖动滑块后立即把对象定位到新时间。
        for (const auto& object : owner->objects_) {
            object->setAnimationTime(timelineTime);
        }

        ImGui::End();
    }

    // ================================================================
    // 3D 点击拾取
    // ================================================================

    /**
     * 射线与三角形求交 (Moller-Trumbore)
     *
     * @param origin 射线起点
     * @param direction 射线方向 (单位向量)
     * @param a,b,c 三角形三个顶点 (世界坐标)
     * @param t 输出: 交点距离
     * @return true 表示相交且交点在射线正方向
     */
    static bool rayTriangle(const glm::vec3& origin,
                            const glm::vec3& direction,
                            const glm::vec3& a,
                            const glm::vec3& b,
                            const glm::vec3& c,
                            float& t) {
        constexpr float epsilon = 1e-6f;

        // 两条边向量。
        const glm::vec3 edge1 = b - a;
        const glm::vec3 edge2 = c - a;

        // p = direction × edge2
        const glm::vec3 p = glm::cross(direction, edge2);

        // 行列式接近 0 说明射线与三角形平面平行。
        const float determinant = glm::dot(edge1, p);
        if (std::abs(determinant) < epsilon) {
            return false;
        }

        const float inverseDeterminant = 1.0f / determinant;

        // 重心坐标 u。
        const glm::vec3 distance = origin - a;
        const float u = glm::dot(distance, p) * inverseDeterminant;
        if (u < 0.0f || u > 1.0f) {
            return false;
        }

        // 重心坐标 v。
        const glm::vec3 q = glm::cross(distance, edge1);
        const float v = glm::dot(direction, q) * inverseDeterminant;
        if (v < 0.0f || u + v > 1.0f) {
            return false;
        }

        // 射线参数 t; 只接受正方向交点。
        t = glm::dot(edge2, q) * inverseDeterminant;
        return t > epsilon;
    }

    /**
     * 把屏幕点击转换成相机射线, 并找出最近的 Object
     *
     * 步骤:
     *   1. 把窗口坐标换算成 framebuffer 坐标;
     *   2. 根据相机 yaw/pitch/distance 构造 view/proj;
     *   3. 用 inv(proj * view) 把 NDC 反投影成世界空间射线;
     *   4. 遍历每个 Object 的每个实例/三角形, 记录最近交点;
     *   5. 命中后更新 selectedObject, 并展开浮窗。
     */
    void pickObject(double mouseX, double mouseY) {
        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;

        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(window, &framebufferWidth,
                               &framebufferHeight);

        if (windowWidth <= 0 || windowHeight <= 0 ||
            framebufferWidth <= 0 || framebufferHeight <= 0) {
            return;
        }

        // GLFW 的鼠标坐标是窗口坐标; 相机投影使用 framebuffer 尺寸,
        // 因此先按 DPI 比例换算。
        const float pixelX =
            static_cast<float>(mouseX) *
            static_cast<float>(framebufferWidth) /
            static_cast<float>(windowWidth);
        const float pixelY =
            static_cast<float>(mouseY) *
            static_cast<float>(framebufferHeight) /
            static_cast<float>(windowHeight);

        // 浮窗停靠时, 3D 只占 sceneViewport 区域。
        VkRect2D viewport = sceneViewport;
        if (viewport.extent.width == 0 || viewport.extent.height == 0) {
            viewport.offset = {0, 0};
            viewport.extent = {
                static_cast<uint32_t>(framebufferWidth),
                static_cast<uint32_t>(framebufferHeight)};
        }

        const float localX =
            pixelX - static_cast<float>(viewport.offset.x);
        const float localY =
            pixelY - static_cast<float>(viewport.offset.y);
        if (localX < 0.0f || localY < 0.0f ||
            localX >= static_cast<float>(viewport.extent.width) ||
            localY >= static_cast<float>(viewport.extent.height)) {
            return;
        }

        // Vulkan NDC: x/y 在 [-1, 1], 且 y 向下。
        const float ndcX =
            2.0f * localX /
                static_cast<float>(viewport.extent.width) - 1.0f;
        const float ndcY =
            2.0f * localY /
                static_cast<float>(viewport.extent.height) - 1.0f;

        const glm::vec3 eye = viewportCameraPosition(camera);
        const glm::mat4 view = glm::lookAt(
            eye, camera.target, viewportCameraUp(camera));

        const float aspect =
            static_cast<float>(viewport.extent.width) /
            static_cast<float>(viewport.extent.height);
        const glm::mat4 projection =
            viewportCameraProjection(camera, aspect);

        const glm::mat4 inverseViewProjection =
            glm::inverse(projection * view);

        glm::vec4 farPoint =
            inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        if (std::abs(farPoint.w) < 1e-6f) {
            return;
        }
        farPoint /= farPoint.w;

        const glm::vec3 rayDirection =
            glm::normalize(glm::vec3(farPoint) - eye);

        float closestDistance = std::numeric_limits<float>::max();
        int hitObject = -1;

        // 局部列表: 不覆盖 renderFrame 复用的 instances。
        std::vector<ObjectInstance> pickInstances;

        for (std::size_t objectIndex = 0;
             objectIndex < owner->objects_.size();
             ++objectIndex) {
            pickInstances.clear();
            owner->objects_[objectIndex]->collectInstances(pickInstances);

            for (const ObjectInstance& instance : pickInstances) {
                if (instance.mesh == nullptr || instance.mesh->empty()) {
                    continue;
                }

                const auto& vertices = instance.mesh->vertices();
                const auto& indices = instance.mesh->indices();

                for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
                    const uint32_t i0 = indices[i];
                    const uint32_t i1 = indices[i + 1];
                    const uint32_t i2 = indices[i + 2];

                    if (i0 >= vertices.size() ||
                        i1 >= vertices.size() ||
                        i2 >= vertices.size()) {
                        continue;
                    }

                    // 把三角形三个顶点变换到世界空间。
                    const glm::vec3 a = glm::vec3(
                        instance.transform *
                        glm::vec4(vertices[i0].position, 1.0f));
                    const glm::vec3 b = glm::vec3(
                        instance.transform *
                        glm::vec4(vertices[i1].position, 1.0f));
                    const glm::vec3 c = glm::vec3(
                        instance.transform *
                        glm::vec4(vertices[i2].position, 1.0f));

                    float distance = 0.0f;
                    if (rayTriangle(eye, rayDirection, a, b, c,
                                    distance) &&
                        distance < closestDistance) {
                        closestDistance = distance;
                        hitObject = static_cast<int>(objectIndex);
                    }
                }
            }
        }

        // 命中: 切换浮窗显示的对象, 并确保浮窗展开。
        if (hitObject >= 0) {
            selectedObject = hitObject;
            hasSelection = true;
            panelCollapsed = false;
            panelScrollToTop = true;
        }
    }

    void renderFrame() {
        if (window == nullptr || !renderer) {
            return;
        }

        // 点击窗口关闭按钮: 有未保存源码时先弹窗确认。
        if (glfwWindowShouldClose(window)) {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            if (hasDirtyEditorDocuments()) {
                pluginEditorVisible = true;
                editorLayoutPending = true;
                editorClosePrompt = true;
                closeProjectAfterEditorPrompt = true;
                return;
            }

            if (owner->menu() != nullptr &&
                owner->menu()->isProjectDirty()) {
                projectClosePrompt = true;
                return;
            }

            hide();
            return;
        }

        if (!visible()) {
            return;
        }

        // 先处理挂起的尺寸变化。交换链重建会生成新的 renderPass,
        // ImGui 的 Vulkan 管线绑定在 renderPass 上, 因此需要同步重建。
        if (!renderer->ensureSwapChainReady()) {
            return;
        }
        if (imguiContext != nullptr &&
            renderer->renderPassVersion() != imguiRenderPassVersion) {
            recreateImGuiVulkanBackend();
        }

        instances.clear();
        for (const auto& object : owner->objects_) {
            object->collectInstances(instances);
        }

        // 选中物体的实例单独收集, 用于绘制黑色描边。
        selectedInstances.clear();
        if (hasSelection &&
            selectedObject >= 0 &&
            selectedObject < static_cast<int>(owner->objects_.size())) {
            owner->objects_[static_cast<std::size_t>(selectedObject)]
                ->collectInstances(selectedInstances);
        }

        // 没有 ImGui 上下文时退化为纯 3D 渲染。
        if (imguiContext == nullptr || !imguiBackendReady) {
            renderer->draw(instances, camera, nullptr,
                           &selectedInstances, sceneViewport);
            return;
        }

        // ---- 构建子窗口自己的 ImGui 帧 ----
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(imguiContext);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (appliedThemeRevision != uiSettingsRevision()) {
            applyMathStudioTheme();
            appliedThemeRevision = uiSettingsRevision();
        }

        drawMenuBar();
        drawFloatingPanel();
        drawTimeline();
        drawPluginEditor();
        drawSettings();
        drawProjectClosePrompt();
        drawViewIndicator();

        ImGui::Render();

        // ImGui 的绘制命令必须记录在同一个 renderPass 内;
        // ViewportRenderer 会在 3D 场景之后调用 RenderDrawData。
        try {
            renderer->draw(instances, camera, ImGui::GetDrawData(),
                           &selectedInstances, sceneViewport);
        } catch (...) {
            ImGui::SetCurrentContext(previous);
            throw;
        }

        ImGui::SetCurrentContext(previous);
    }

    void beginDrag(double x, double y) {
        lastMouseX = x;
        lastMouseY = y;
    }

    void updateCameraFromMouse(double x, double y) {
        const double dx = x - lastMouseX;
        const double dy = y - lastMouseY;
        lastMouseX = x;
        lastMouseY = y;

        if (rotating) {
            // 中键拖动: 围绕 target 做球面轨道旋转。
            // 方向与鼠标移动方向相反, 更符合"拖动画面"的手感。
            constexpr float sensitivity = 0.005f;
            camera.yaw -= static_cast<float>(dx) * sensitivity;
            camera.pitch += static_cast<float>(dy) * sensitivity;
            // 限制俯仰角, 避免相机翻转到包围球另一侧。
            camera.pitch = std::clamp(camera.pitch, -1.45f, 1.45f);
        } else if (panning) {
            // 右键拖动: 在相机的 right/up 平面内平移观察目标。
            const glm::vec3 eye = viewportCameraPosition(camera);
            const glm::vec3 forward = glm::normalize(camera.target - eye);
            const glm::vec3 right =
                glm::normalize(glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f)));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));

            const float panScale = camera.distance * 0.0015f;
            camera.target +=
                (-right * static_cast<float>(dx) +
                 up * static_cast<float>(dy)) * panScale;
        }

        if (rotating || panning) {
            markProjectDirty();
        }
    }

    static void mouseButtonCallback(GLFWwindow* glfwWindow, int button,
                                    int action, int) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
        if (self == nullptr) {
            return;
        }

        // 先转发给 ImGui, 让浮窗可以接收点击。
        self->feedMouseButton(button, action);

        // 松开按键时必须结束相机拖动, 即使鼠标当前在浮窗上。
        if (button == GLFW_MOUSE_BUTTON_MIDDLE &&
            action == GLFW_RELEASE) {
            self->rotating = false;
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT &&
            action == GLFW_RELEASE) {
            self->panning = false;
        }

        // 鼠标在浮窗上时, 不要开始旋转/平移。
        if (self->imguiWantsMouse()) {
            return;
        }

        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(glfwWindow, &x, &y);

        // 左键点击 3D 场景: 拾取物体并切换参数浮窗的对象。
        if (button == GLFW_MOUSE_BUTTON_LEFT &&
            action == GLFW_PRESS) {
            self->pickObject(x, y);
            return;
        }

        if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (action == GLFW_PRESS) {
                self->rotating = true;
                self->panning = false;
                self->beginDrag(x, y);
            }
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                self->panning = true;
                self->rotating = false;
                self->beginDrag(x, y);
            }
        }
    }

    static void cursorPosCallback(GLFWwindow* glfwWindow, double x, double y) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
        if (self == nullptr) {
            return;
        }

        // 浮窗需要实时鼠标位置来做悬停/拖动。
        self->feedMousePos(x, y);

        if (self->rotating || self->panning) {
            self->updateCameraFromMouse(x, y);
        }
    }

    static void focusCallback(GLFWwindow* glfwWindow, int focused) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
        if (self == nullptr) {
            return;
        }

        self->feedFocus(focused != 0);

        if (focused == GLFW_FALSE) {
            self->rotating = false;
            self->panning = false;
        }
    }

    static void scrollCallback(GLFWwindow* glfwWindow, double, double yOffset) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
        if (self == nullptr) {
            return;
        }

        // 先给浮窗滚动 (例如滚动参数列表)。
        self->feedScroll(yOffset);

        // 鼠标在浮窗上时, 不要缩放 3D 相机。
        if (self->imguiWantsMouse()) {
            return;
        }

        // 滚轮向上 (yOffset > 0) 拉近相机, 向下推远相机。
        // 使用指数缩放, 远近不同距离下的缩放幅度都比较自然。
        const float factor = std::pow(0.9f, static_cast<float>(yOffset));
        self->camera.distance =
            std::clamp(self->camera.distance * factor, 0.5f, 100.0f);
        self->markProjectDirty();
    }

    /** 小键盘视图预设: 1 前, 2 后, 3 上, 7 下, 8 左, 9 右 */
    bool applyCameraViewPreset(int key) {
        constexpr float halfPi = 1.57079632679f;

        const bool valid =
            key == GLFW_KEY_KP_1 || key == GLFW_KEY_KP_2 ||
            key == GLFW_KEY_KP_3 || key == GLFW_KEY_KP_7 ||
            key == GLFW_KEY_KP_8 || key == GLFW_KEY_KP_9;
        if (!valid) {
            return false;
        }

        // 第一次进入正交视图时保存透视相机, Esc 时恢复。
        if (!camera.orthographic) {
            perspectiveCamera = camera;
            hasPerspectiveCamera = true;
        }

        // 标准视图采用正交投影, 否则不同深度的物体会产生透视缩放。
        camera.orthographic = true;
        markProjectDirty();

        switch (key) {
        case GLFW_KEY_KP_1:  // 前视图 (从 -Y 看向 +Y)
            camera.yaw = -halfPi;
            camera.pitch = 0.0f;
            viewIndicator = "前";
            return true;
        case GLFW_KEY_KP_2:  // 后视图
            camera.yaw = halfPi;
            camera.pitch = 0.0f;
            viewIndicator = "后";
            return true;
        case GLFW_KEY_KP_3:  // 顶视图
            camera.yaw = -halfPi;
            camera.pitch = halfPi;
            viewIndicator = "上";
            return true;
        case GLFW_KEY_KP_7:  // 底视图
            camera.yaw = -halfPi;
            camera.pitch = -halfPi;
            viewIndicator = "下";
            return true;
        case GLFW_KEY_KP_8:  // 左视图
            camera.yaw = 3.14159265359f;
            camera.pitch = 0.0f;
            viewIndicator = "左";
            return true;
        case GLFW_KEY_KP_9:  // 右视图
            camera.yaw = 0.0f;
            camera.pitch = 0.0f;
            viewIndicator = "右";
            return true;
        default:
            return false;
        }
    }

    /** Esc: 退出正交视图, 恢复进入前的透视相机 */
    void exitOrthographicView() {
        if (!camera.orthographic) {
            return;
        }

        if (hasPerspectiveCamera) {
            camera = perspectiveCamera;
        }
        camera.orthographic = false;
        viewIndicator.clear();
    }

    static void keyCallback(GLFWwindow* glfwWindow, int key, int,
                            int action, int mods) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
        if (self == nullptr) {
            return;
        }

        // Esc: 正交视图 -> 返回进入前的透视视图。
        if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE &&
            self->camera.orthographic &&
            !self->imguiWantsTextInput()) {
            self->exitOrthographicView();
            return;
        }

        // 配置中的快捷键优先处理。
        if (action == GLFW_PRESS && !self->imguiWantsTextInput()) {
            const AppSettings& settings = appSettings();
            Menu* projectMenu = self->owner->menu();

            if (projectMenu != nullptr) {
                if (shortcutMatches(settings.saveShortcut, key, mods)) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::Save);
                    return;
                }
                if (shortcutMatches(settings.saveAsShortcut, key, mods)) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::SaveAs);
                    return;
                }
                if (shortcutMatches(settings.openShortcut, key, mods)) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::Open);
                    return;
                }
                if (shortcutMatches(settings.newShortcut, key, mods)) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::New);
                    return;
                }
                if (shortcutMatches(settings.closeShortcut, key, mods)) {
                    projectMenu->requestProjectCommand(
                        Menu::ProjectCommand::Close);
                    return;
                }
            }

            if (shortcutMatches(settings.settingsShortcut, key, mods)) {
                self->settingsVisible = true;
                self->settingsScrollToTop = true;
                return;
            }
            if (shortcutMatches(settings.editorShortcut, key, mods)) {
                self->pluginEditorVisible = !self->pluginEditorVisible;
                if (self->pluginEditorVisible) {
                    self->editorLayoutPending = true;
                }
                return;
            }
            if (shortcutMatches(settings.timelineShortcut, key, mods)) {
                self->timelineVisible = !self->timelineVisible;
                return;
            }
        }

        // 小键盘视图预设。
        if (action == GLFW_PRESS && !self->imguiWantsTextInput()) {
            if (self->applyCameraViewPreset(key) &&
                self->dockMode == PanelDock::Floating) {
                // 浮动参数窗会遮住视图中心; 切换标准视图时自动收起,
                // 保证前/后/左/右/顶/底视图完整可见。
                self->panelCollapsed = true;
            }
        }

        self->feedKey(key, action, mods);
    }

    static void charCallback(GLFWwindow* glfwWindow, unsigned int codepoint) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
        if (self != nullptr) {
            self->feedChar(codepoint);
        }
    }

    static void framebufferResizeCallback(GLFWwindow* glfwWindow, int, int) {
        auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
        if (self != nullptr && self->renderer) {
            self->renderer->onFramebufferResized();
        }
    }
};

// ====================================================================
// SubWindow 公共接口
// ====================================================================

SubWindow::SubWindow(std::shared_ptr<const ViewportContext> context,
                     std::string title,
                     int width,
                     int height)
    : title_(std::move(title)),
      impl_(std::make_unique<Impl>(std::move(context), this, width, height)) {
    if (!impl_->context) {
        throw std::invalid_argument("SubWindow: ViewportContext is null");
    }
}

SubWindow::~SubWindow() = default;

const std::string& SubWindow::title() const {
    return title_;
}

void SubWindow::setTitle(std::string title) {
    title_ = std::move(title);
    refresh();
}

Object& SubWindow::addObject(std::unique_ptr<Object> object) {
    if (!object) {
        throw std::invalid_argument("SubWindow::addObject: object is null");
    }

    Object& reference = *object;
    objects_.push_back(std::move(object));
    hierarchy_.addObject(reference.id());
    onObjectsChanged();
    return reference;
}

bool SubWindow::removeObject(std::size_t index) {
    if (index >= objects_.size()) {
        return false;
    }

    hierarchy_.removeObject(objects_[index]->id());
    objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(index));
    onObjectsChanged();
    return true;
}

void SubWindow::clearObjects() {
    objects_.clear();
    hierarchy_.clear();
    onObjectsChanged();
}

std::size_t SubWindow::objectCount() const {
    return objects_.size();
}

Object* SubWindow::objectAt(std::size_t index) {
    if (index >= objects_.size()) {
        return nullptr;
    }
    return objects_[index].get();
}

const Object* SubWindow::objectAt(std::size_t index) const {
    if (index >= objects_.size()) {
        return nullptr;
    }
    return objects_[index].get();
}

const std::vector<std::unique_ptr<Object>>& SubWindow::objects() const {
    return objects_;
}

ObjectHierarchy& SubWindow::hierarchy() {
    return hierarchy_;
}

const ObjectHierarchy& SubWindow::hierarchy() const {
    return hierarchy_;
}

Object* SubWindow::objectById(ObjectId id) {
    for (const auto& object : objects_) {
        if (object->id() == id) {
            return object.get();
        }
    }
    return nullptr;
}

const Object* SubWindow::objectById(ObjectId id) const {
    for (const auto& object : objects_) {
        if (object->id() == id) {
            return object.get();
        }
    }
    return nullptr;
}

std::size_t SubWindow::indexOfObject(ObjectId id) const {
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        if (objects_[i]->id() == id) {
            return i;
        }
    }
    return objects_.size();
}

ViewportCamera SubWindow::camera() const {
    return impl_->camera;
}

void SubWindow::setCamera(const ViewportCamera& camera) {
    impl_->camera = camera;
    if (menu_ != nullptr) {
        menu_->markProjectDirty();
    }
}

void SubWindow::update(float deltaSeconds) {
    if (impl_) {
        impl_->advanceTimeline(deltaSeconds);
    }
}

void SubWindow::render() {
    if (impl_) {
        impl_->renderFrame();
    }
}

void SubWindow::show() {
    if (!impl_ || !impl_->show()) {
        throw std::runtime_error("创建子窗口失败!");
    }
    refresh();
}

void SubWindow::hide() {
    if (impl_) {
        impl_->hide();
    }
}

bool SubWindow::isVisible() const {
    return impl_ && impl_->visible();
}

void SubWindow::refresh() {
    if (!impl_) {
        return;
    }

    // 工程窗口的标题只显示工程名, 不再显示"对象数量/场景"信息。
    impl_->setTitle(title_);
}

void SubWindow::setMenu(Menu* menu) {
    menu_ = menu;
}

Menu* SubWindow::menu() const {
    return menu_;
}

void SubWindow::setPanelCollapsed(bool collapsed) {
    if (impl_) {
        impl_->panelCollapsed = collapsed;
    }
}

void SubWindow::onObjectsChanged() {
    if (menu_ != nullptr) {
        menu_->markProjectDirty();
    }
    refresh();
}












