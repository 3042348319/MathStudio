# Vulkan 完整示例 —— 构建过程文档

本文档记录了本示例项目从零开始构建、编译、运行的完整过程，基于教程
[Khronos Vulkan 教程（中文版）](https://docs.vulkan.net.cn/tutorial/latest/00_Introduction.html)
编写。所有命令均在本文档记录的实际环境中执行并验证通过。

---

## 1. 项目简介

| 项目 | 说明 |
|------|------|
| 名称 | VulkanTutorial |
| 效果 | 一个贴着棋盘格纹理的彩色立方体绕 Z 轴旋转，带深度测试与 Mipmap，窗口可任意拉伸 |
| 语言 | C++17 |
| 窗口库 | GLFW 3.4（教程同款） |
| 数学库 | GLM 1.0.2（教程同款） |
| 构建系统 | CMake 3.20+ + Visual Studio 2022 |
| 依赖管理 | vcpkg |

### 与教程章节的对应关系

本示例完整覆盖教程以下章节（`main.cpp` 中每个函数的注释都标注了所属章节）：

| 教程章节 | 对应代码 |
|----------|----------|
| 03-00 基础代码 | `run()` / `initWindow()` / `mainLoop()` |
| 03-01 实例 | `createInstance()` / `getRequiredExtensions()` |
| 03-02 验证层 | `checkValidationLayerSupport()` / `setupDebugMessenger()` / `debugCallback()` |
| 03-03 物理设备和队列族 | `pickPhysicalDevice()` / `rateDeviceSuitability()` / `QueueFamilyIndices` |
| 03-04 逻辑设备和队列 | `createLogicalDevice()` |
| 04-00 窗口表面 | `createSurface()` |
| 04-01 交换链 | `createSwapChain()` / `querySwapChainSupport` / 三个 `choose*()` 函数 |
| 04-02 图像视图 | `createImageViews()` / `createImageView()` |
| 05-01 着色器模块 | `createShaderModule()` / `loadShaderCode()` / `shaders/` 目录 |
| 05-02 固定函数 | `createGraphicsPipeline()` 中的各种状态结构体 |
| 05-03 渲染通道 | `createRenderPass()` |
| 06-00 帧缓冲 | `createFramebuffers()` |
| 06-01 命令缓冲 | `createCommandPool()` / `createCommandBuffers()` / `recordCommandBuffer()` |
| 06-02 渲染和演示 | `drawFrame()` |
| 06-03 飞行中的帧 | `MAX_FRAMES_IN_FLIGHT` / `inFlightFences` / `imagesInFlight` |
| 交换链重建 | `recreateSwapChain()` / `cleanupSwapChain()` / `framebufferResizeCallback()` |
| 顶点缓冲 | `createVertexBuffer()` / `Vertex::getBindingDescription()` |
| 暂存缓冲 | `createBuffer()` / `copyBuffer()` / `beginSingleTimeCommands()` |
| 索引缓冲 | `createIndexBuffer()` |
| 统一缓冲 | `UniformBufferObject` / `createUniformBuffers()` / `updateUniformBuffer()` |
| 描述符集布局和缓冲 | `createDescriptorSetLayout()` |
| 描述符池和集合 | `createDescriptorPool()` / `createDescriptorSets()` |
| 图像 | `createTextureImage()`（纹理为**程序化生成的棋盘格**，不依赖图片文件） |
| 图像视图和采样器 | `createTextureImageView()` / `createTextureSampler()` |
| 组合图像采样器 | `createDescriptorSets()` 中的 binding 1 |
| 深度缓冲 | `createDepthResources()` / `findDepthFormat()` |
| 生成 Mipmap | `generateMipmaps()` / `vkCmdBlitImage` |

未包含的章节（可自行扩展）：**加载模型**（顶点数据为硬编码立方体）、
**多重采样**（`samples = 1`）、**计算着色器**。

---

## 2. 环境准备

### 2.1 本机实际环境（本次构建使用）

| 工具 | 版本/路径 | 说明 |
|------|-----------|------|
| Windows | Windows 11 Home 10.0.26200 | |
| Visual Studio | 2022 Community，MSVC 19.44 | 需勾选"使用 C++ 的桌面开发"工作负载 |
| CMake | 3.x（`C:\Program Files\CMake`） | VS2022 自带，也可单独安装 |
| Vulkan SDK | **1.4.341.1**（`C:\VulkanSDK\1.4.341.1`） | LunarG 官方 SDK |
| vcpkg | `C:\dev\vcpkg` | C/C++ 包管理器 |
| glfw3 | 3.4（vcpkg 安装） | 窗口库 |
| glm | 1.0.2（vcpkg 安装） | 数学库 |

### 2.2 安装 Vulkan SDK

1. 从 <https://vulkan.lunarg.com/sdk/windows> 下载 Windows 版 SDK 安装包；
2. 运行安装器（**务必勾选 Shader Toolchain**，即 glslc 编译器）；
3. 安装器会自动设置环境变量 `VULKAN_SDK`，指向 `C:\VulkanSDK\<版本号>`；
4. 验证安装：

```bash
echo %VULKAN_SDK%
%VULKAN_SDK%\Bin\glslc --version   # 应输出 shaderc 版本信息
```

> 提示：SDK 自带 `vkvia.exe`（Vulkan 安装验证工具），
> 双击运行可检查显卡驱动是否支持 Vulkan。

### 2.3 安装 vcpkg（依赖 GLFW 和 GLM）

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat        # 生成 vcpkg.exe
setx VCPKG_ROOT C:\dev\vcpkg # 设置环境变量（可选，方便 CMake 定位）
```

### 2.4 安装依赖包

```bash
vcpkg install glfw3:x64-windows
vcpkg install glm:x64-windows
```

安装完成后，依赖位于 `vcpkg\installed\x64-windows\`：

- `include\GLFW\glfw3.h`、`lib\glfw3dll.lib`、`bin\glfw3.dll`
- `include\glm\glm.hpp`（GLM 是纯头文件库）

> 注意：如果显卡驱动太旧，还可能需要安装 `glslang` 相关的
> 着色器编译组件，本示例使用 SDK 自带的 glslc，无需额外安装。

---

## 3. 项目结构

```
math/
├── CMakeLists.txt            # 构建脚本（含 glslc 着色器编译规则）
├── main.cpp                  # 全部源码（~1400 行，每个函数/结构体均有中文注释）
├── shaders/
│   ├── shader.vert           # 顶点着色器（GLSL 源码）
│   └── shader.frag           # 片元着色器（GLSL 源码）
├── build/                    # 构建输出（cmake 生成，含 .spv 与 exe）
│   ├── shaders/
│   │   ├── vert.spv          # 由 glslc 编译出的 SPIR-V 字节码
│   │   └── frag.spv
│   ├── Debug/VulkanTutorial.exe
│   └── Release/VulkanTutorial.exe
└── 构建Vulkan项目过程.md      # 本文档
```

### CMakeLists.txt 要点说明

| 段落 | 作用 |
|------|------|
| `find_package(Vulkan REQUIRED)` | 通过 `VULKAN_SDK` 环境变量定位 SDK，提供 `Vulkan::Vulkan` 链接目标 |
| `find_package(glfw3 3.3 REQUIRED)` | 定位 vcpkg 安装的 GLFW |
| `find_package(glm REQUIRED)` | 定位 GLM（`glm::glm` 头文件目标） |
| `find_program(GLSLC glslc ...)` | 查找 SDK 中的 glslc 着色器编译器 |
| `add_custom_command(...)` ×2 | 把 `shader.vert/frag` 编译为 `build/shaders/*.spv`，源码改动时自动重编 |
| `add_executable(... WIN32 main.cpp)` | WIN32 = GUI 程序（不弹控制台） |
| `/ENTRY:mainCRTStartup` | MSVC 下让普通 `main()` 可用于 GUI 子系统 |

---

## 4. 配置与编译（本次构建实际执行的命令）

### 4.1 配置（生成 VS 工程）

在项目根目录执行：

```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
```

关键参数说明：

- `-B build`：生成到 `build` 目录；
- `-G "Visual Studio 17 2022" -A x64`：VS2022 生成器，64 位平台
  （Vulkan SDK 的 vulkan-1.lib 是 64 位的，32 位会链接失败）；
- `-DCMAKE_TOOLCHAIN_FILE=...vcpkg.cmake`：**关键一步**，
  让 CMake 能通过 `find_package` 找到 vcpkg 安装的 glfw3 和 glm。
  也可以设置环境变量 `VCPKG_ROOT` 后由 vcpkg 工具链自动生效。

本次配置输出确认：

```
-- Found Vulkan: C:/VulkanSDK/1.4.341.1/Lib/vulkan-1.lib (found version "1.4.341")
-- Configuring done (4.0s)
-- Build files have been written to: D:/Program/VulkanProject/math/build
```

### 4.2 编译

```bash
# Debug 版（启用验证层，开发期推荐）
cmake --build build --config Debug

# Release 版（不启用验证层，性能最高）
cmake --build build --config Release
```

编译日志中可以看到两个着色器被 glslc 编译：

```
编译片元着色器 shader.frag -> frag.spv
编译顶点着色器 shader.vert -> vert.spv
main.cpp
VulkanTutorial.vcxproj -> D:\Program\VulkanProject\math\build\Debug\VulkanTutorial.exe
```

也可以在 Visual Studio 中直接打开 `build\VulkanTutorial.sln` 编译运行。

---

## 5. 运行

### 5.1 从 Visual Studio 运行（推荐）

打开 `build\VulkanTutorial.sln`，将 `VulkanTutorial` 设为启动项目，按 **F5**。
CMake 已把 VS 调试器工作目录设为 `build\shaders`，着色器文件可直接找到。

### 5.2 直接运行 exe

```bash
cd build\Debug      # 或 build\Release
VulkanTutorial.exe
```

### 5.3 着色器文件查找规则

程序按以下顺序查找着色器（`loadShaderCode()` 实现），对应不同运行位置：

```
shaders/vert.spv      ← 在 build 目录运行
vert.spv              ← VS F5 调试（工作目录 = build/shaders）
../shaders/vert.spv   ← 在 build/Debug 直接运行
../../shaders/vert.spv ← 在更深目录运行
```

运行效果：深色背景中，一个贴棋盘格纹理的彩色立方体绕 Z 轴旋转
（每 4 秒一圈），每面颜色不同；拉伸窗口时交换链自动重建，画面不拉伸变形；
最小化后恢复也能正常工作。

---

## 6. 构建验证过程（本次实际遇到的 4 个问题与修复）

Debug 版启用了验证层 `VK_LAYER_KHRONOS_validation`，
它会在程序运行时报出任何 API 使用错误——本次构建中它抓到了 3 个真实 bug：

### 6.1 编译错误：缺少 `<map>` 头文件

```
error C2039: "multimap": 不是 "std" 的成员
```

`pickPhysicalDevice()` 用 `std::multimap` 给物理设备按分数排序，
忘了包含 `<map>`。**修复**：添加 `#include <map>`。

### 6.2 验证层：Mipmap 布局转换错误

```
vkCmdPipelineBarrier(): ... cannot transition the layout of aspect=1, level=1
from VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when the previously used layout
is VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
```

原因：Mipmap 生成循环的第一道屏障把第 0 级布局从 `SHADER_READ_ONLY`
转到 `TRANSFER_SRC`，但第 0 级刚接收完暂存缓冲拷贝，实际布局是
`TRANSFER_DST`。**修复**：第一道屏障改为 `TRANSFER_DST → TRANSFER_SRC`
（访问掩码 `TRANSFER_WRITE → TRANSFER_READ`，管线阶段两端均为 `TRANSFER`）。

### 6.3 验证层：交换链信号量复用冲突

```
vkQueueSubmit(): pSignalSemaphores[0] ... may still be in use by VkSwapchainKHR
Swapchain image 1 was presented but was not re-acquired, so VkSemaphore ...
may still be in use and cannot be safely reused
```

原因：在 MAILBOX 呈现模式下，交换链图像数（minImageCount+1，通常 3~4 张）
大于飞行帧数（2），按帧索引复用"渲染完成"信号量时，呈现引擎可能还没消费完
该信号量（呈现引擎的消费时机不受围栏约束）。

**修复**（这是教程之外的重要细节，注释里已详细说明）：

- `renderFinishedSemaphores` 改为**每个交换链图像一个**（按 `imageIndex` 索引）
  —— 只有重新获取同一张图像（呈现引擎已释放它）才能保证该图像信号量空闲；
- `imageAvailableSemaphores` 保持**每个飞行帧一个**（按 `currentFrame` 索引）
  —— 复用帧槽位前围栏已保证上一帧提交完成，该信号量必然已被消费，安全。

### 6.4 验证层：屏障阶段掩码不匹配

```
vkCmdPipelineBarrier(): srcAccessMask (TRANSFER_WRITE_BIT) is not supported
by stage mask (FRAGMENT_SHADER_BIT)
```

修复 6.2 时改了访问掩码，但忘了同步修改管线阶段。**修复**：源/目标阶段
都改为 `VK_PIPELINE_STAGE_TRANSFER_BIT`。

**最终结果：重新构建后验证层输出 0 行错误，Debug 与 Release 版本均
冒烟测试通过（运行 10 秒无崩溃、无验证层报错）。**

---

## 7. 常见问题排查

| 现象 | 原因与解决 |
|------|-----------|
| `找不到 glslc!` | 未安装 Vulkan SDK 的 Shader Toolchain 组件，或 `VULKAN_SDK` 环境变量未设置。重装 SDK 并重启终端 |
| `LNK2019: 无法解析的外部符号 vkCreateInstance` | 未链接 `Vulkan::Vulkan`（vulkan-1.lib），或用了 32 位配置（vcpkg 库与 SDK 均需 x64） |
| `找不到 glfw3Config.cmake` | 没加 `-DCMAKE_TOOLCHAIN_FILE` 参数，CMake 找不到 vcpkg 的包 |
| 运行时提示"找不到着色器文件" | exe 的工作目录不对，把工作目录设为 `build/shaders`，或从 `build` 目录运行 |
| `请求的验证层不可用` | 显卡驱动过旧或未装 SDK 的验证层；或 Release 模式下误改了 `enableValidationLayers` |
| 验证层报 `oldLayout must be ...` | 图像布局转换与图像当前实际布局不符，对照 `transitionImageLayout` 调用顺序检查 |
| 程序运行但窗口全黑 | 检查交换链格式是否匹配渲染通道；用 Debug 版看验证层输出定位 |
| 窗口最小化后程序"卡住" | 本示例已在 `recreateSwapChain` 中用 `glfwWaitEvents` 处理，若仍卡住检查是否走了该分支 |
| 拉伸窗口报错/黑屏 | 确认 `framebufferResizeCallback` 已注册，`drawFrame` 处理了 `VK_SUBOPTIMAL_KHR` |

### 调试技巧：如何看到验证层输出

程序是 GUI 子系统（WIN32），双击运行看不到 `std::cerr` 输出。三种方式：

1. **VS 调试器**：F5 运行时消息显示在"输出"窗口；
2. **命令行重定向**：`VulkanTutorial.exe > log.txt 2>&1`；
3. 临时把 CMakeLists 中的 `WIN32` 去掉重新构建，即可恢复控制台窗口。

---

## 8. 代码导读

`main.cpp` 按教程章节顺序组织，阅读建议：

1. **先看成员变量区**（"四、VulkanTutorialApp 类"）——了解要创建哪些 Vulkan 对象；
2. **再看 `initVulkan()`**——它按依赖顺序编排了 21 个创建函数，
   每个函数对应教程一个章节；
3. **重点理解 `drawFrame()`**——每帧的完整流程：
   等围栏 → 取图像 → 更新 UBO → 记录命令 → 提交 → 呈现 → 处理重建；
4. **对比 `recreateSwapChain()` 与 `initVulkan()`**——体会哪些资源
   依赖交换链尺寸（要重建）、哪些不依赖（不用重建）；
5. **`generateMipmaps()` 与 `transitionImageLayout()`**——Vulkan 最核心的
   同步与布局转换概念。

### 扩展练习建议

- 把棋盘格纹理换成从图片文件加载（需要 stb_image 或自己解析 PNG）；
- 加入相机控制（WASD + 鼠标旋转视角，改 UBO 的 view 矩阵）；
- 实现教程的**多重采样**章节（渲染通道加 resolve 附件，`samples` 改 4）；
- 实现教程的**加载模型**章节（用 tinyobjloader 加载 .obj 文件替换硬编码立方体）。

---

*本文档与示例代码基于 [docs.vulkan.net.cn](https://docs.vulkan.net.cn/tutorial/latest/00_Introduction.html) 教程编写，构建过程于 2026-10 在 Windows 11 + Vulkan SDK 1.4.341.1 环境实际执行验证。*
