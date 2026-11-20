# 重构完成

- 目标目录：`D:\Program\VulkanProject\math`
- 暂存目录：`C:\Users\30423\Documents\deepseek-harness\default-workspace\math-refactor`
- 完成时间：2026-10-20 16:36（Asia/Shanghai）

## 最终结果

| 文件 | 说明 |
| --- | --- |
| `window.h` | 常量、辅助结构体、`VulkanTutorialApp` 单例类声明；每个函数均有功能/参数/返回值注释 |
| `window.cpp` | 所有结构体成员函数与 `VulkanTutorialApp` 成员函数的定义，保留实现步骤注释 |
| `main.cpp` | 仅保留程序入口，通过 `VulkanTutorialApp::getInstance().run()` 运行 |
| `CMakeLists.txt` | `add_executable` 已加入 `window.cpp`、`window.h` |

## 单例实现

- 构造函数、析构函数私有；
- 拷贝构造、拷贝赋值、移动构造、移动赋值全部 `delete`；
- 唯一入口 `static VulkanTutorialApp& getInstance()`，内部使用函数内静态局部变量；
- 整个程序最多只有一个 `VulkanTutorialApp` 对象。

## 验证

- 使用 MSVC 直接编译 `main.cpp` + `window.cpp`：编译、链接通过；
- 使用项目 CMake 构建：`VulkanTutorial.vcxproj -> D:\Program\VulkanProject\math\build\Release\VulkanTutorial.exe`，退出码 0；
- 源文件哈希与暂存目录一致；
- 着色器 `vert.spv` / `frag.spv` 已存在。

## 备注

MSBuild 在本机首次构建时因环境变量 `NO_PROXY` 与 `no_proxy` 大小写重复
报 `MSB6001`（属于环境问题，不是代码问题）。使用清理后的环境变量启动
CMake 构建后成功。若在 Visual Studio 中遇到同样错误，删除其中一个重复的
环境变量即可。
