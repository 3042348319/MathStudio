// ====================================================================
// Vulkan 完整示例 —— 程序入口 (main.cpp)
// 参照教程: https://docs.vulkan.net.cn/tutorial/latest/00_Introduction.html
//
// 窗口创建、Vulkan 初始化与渲染逻辑的声明/定义已拆分到:
//   * window.h  —— 常量、辅助结构体、VulkanTutorialApp 单例类声明
//   * window.cpp —— 上述所有函数的实现
//
// 本文件只保留程序入口: 获取全局唯一的 VulkanTutorialApp 实例并运行。
// ====================================================================

#include "window.h"

#include <cstdlib>    // EXIT_SUCCESS / EXIT_FAILURE
#include <exception>  // std::exception
#include <iostream>   // std::cerr

/**
 * 程序入口
 * 通过单例访问接口获取全局唯一的应用对象并运行, 捕获所有异常:
 * Vulkan 初始化流程中任何一步失败都会抛出 std::runtime_error,
 * 这里统一打印错误信息并返回失败码。
 */
int main() {
    try {
        // VulkanTutorialApp 的构造函数是私有的, 只能通过 getInstance()
        // 获取全局唯一实例 (整个程序最多存在一个这样的对象)。
        VulkanTutorialApp::getInstance().run();
    } catch (const std::exception& e) {
        std::cerr << "程序异常退出: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
