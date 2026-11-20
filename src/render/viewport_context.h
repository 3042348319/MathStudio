// ====================================================================
// viewport_context.h —— 子窗口共享的 Vulkan 资源
//
// VulkanTutorialApp 创建并持有 instance / device / 队列 / 命令池 /
// 共享立方体顶点索引缓冲; 子窗口通过 ViewportContext 复用这些资源,
// 每个子窗口只创建自己的 surface / swapchain / 管线 / 同步对象。
//
// 该结构体只保存"借用"的句柄, 不负责销毁; 生命周期由 VulkanTutorialApp
// 保证 (菜单和子窗口先于 Vulkan 资源销毁)。
// ====================================================================
#pragma once

// 与 window.h 中的定义保持一致 (重复定义同样的宏是安全的)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GLFW/glfw3.h>

#include <cstdint>

struct ViewportContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // 共享的立方体几何 (与主窗口使用的是同一份)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;

    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
};
