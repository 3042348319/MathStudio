// ====================================================================
// viewport_renderer.h —— 子窗口的 Vulkan 渲染器
//
// 每个 SubWindow 拥有一个 ViewportRenderer:
//   * 使用共享的 VkInstance / VkDevice / 队列 / 命令池;
//   * 自己创建 surface、swapchain、depth、render pass、pipeline、
//     framebuffer、UBO、描述符集和同步对象;
//   * draw() 接收一组 ObjectInstance, 按每个实例自己的 Mesh
//     绑定顶点/索引缓冲并绘制, 因此渲染器不需要知道 Object/Plugin 类型。
// ====================================================================
#pragma once

#include "object.h"
#include "viewport_context.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

// ImGui 的绘制数据; 只需要指针, 不必在头文件里包含整个 imgui.h。
struct ImDrawData;

/**
 * 子窗口相机
 * 每个 SubWindow 持有独立的一份; 通过鼠标中键/右键修改。
 * 相机围绕 target 做球面轨道运动。
 */
struct ViewportCamera {
    glm::vec3 target{0.0f};                 // 观察目标 (轨道中心)
    float yaw = 0.78539816339f;             // 水平角 (45°)
    float pitch = 0.52359877560f;           // 俯仰角 (30°)
    float distance = 7.0f;                  // 相机到目标的距离 (滚轮缩放)
    float fov = 0.78539816339f;             // 垂直视场角 (45°)
    float nearPlane = 0.1f;                 // 近裁剪面
    float farPlane = 100.0f;                // 远裁剪面

    // 标准前/后/左/右/顶/底视图使用正交投影,
    // 避免混合场景中不同深度的物体产生透视缩放。
    bool orthographic = false;
};

/** 根据 yaw/pitch/distance 计算相机世界坐标 */
glm::vec3 viewportCameraPosition(const ViewportCamera& camera);

/**
 * 相机上方向
 * 顶视图/底视图时固定用 (0,0,1) 会让 lookAt 退化,
 * 因此俯仰角接近 ±90° 时改用世界 Y 轴作为屏幕上方。
 */
glm::vec3 viewportCameraUp(const ViewportCamera& camera);

/**
 * 根据相机状态生成投影矩阵
 * 标准视图使用正交投影; 自由视图使用透视投影。
 * 渲染和鼠标拾取必须使用同一个投影矩阵。
 */
glm::mat4 viewportCameraProjection(const ViewportCamera& camera,
                                   float aspect);

/**
 * 子窗口 Vulkan 渲染器
 */
class ViewportRenderer {
public:
    /**
     * 构造渲染器并创建全部 Vulkan 资源
     * @param context 主程序共享的 Vulkan 资源
     * @param window 子窗口的 GLFW 窗口 (必须使用 GLFW_NO_API)
     * @throw std::runtime_error Vulkan 资源创建失败时抛出
     */
    ViewportRenderer(std::shared_ptr<const ViewportContext> context,
                     GLFWwindow* window);

    ~ViewportRenderer();

    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;
    ViewportRenderer(ViewportRenderer&&) = delete;
    ViewportRenderer& operator=(ViewportRenderer&&) = delete;

    /**
     * 使用指定相机绘制一帧
     * @param instances   对象实例
     * @param camera      相机
     * @param overlay     ImGui 绘制数据; 非空时在 3D 场景之后叠加绘制
     * @param outline     选中物体的实例列表; 非空时绘制黑色描边
     * @param sceneViewport 3D 场景在交换链中的矩形区域
     *                      (停靠浮窗时只占剩余区域)
     */
    void draw(const std::vector<ObjectInstance>& instances,
              const ViewportCamera& camera,
              ImDrawData* overlay = nullptr,
              const std::vector<ObjectInstance>* outline = nullptr,
              VkRect2D sceneViewport = VkRect2D{});

    /**
     * 处理挂起的窗口尺寸变化
     * 如果交换链需要重建, 会在这里完成重建, 便于调用方在构建
     * ImGui 帧之前判断 renderPass 是否已经变化。
     * @return false 表示窗口最小化/交换链不可用
     */
    bool ensureSwapChainReady();

    /** 当前 renderPass (ImGui Vulkan 后端初始化时需要) */
    VkRenderPass renderPass() const;

    /** 当前交换链图像数量 (ImGui_ImplVulkan_InitInfo::ImageCount) */
    uint32_t imageCount() const;

    /**
     * renderPass 版本号
     * 每次交换链重建都会递增; ImGui 的管线绑定在 renderPass 上,
     * 调用方据此判断是否需要重建 ImGui Vulkan 后端。
     */
    uint32_t renderPassVersion() const;

    /** GLFW 帧缓冲尺寸变化时调用, 下一帧重建交换链 */
    void onFramebufferResized();

    /** 等待 GPU 空闲 (销毁前使用) */
    void waitIdle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
