# MathStudio

Vulkan + C++17 + Dear ImGui 的数学场景编辑器。

## 功能

- Vulkan 3D 视口、正交标准视图、时间轴
- Object 基本体、组别、可见性眼睛开关
- 静态 C++ 插件与动态 DLL 插件
- 插件代码编辑器：静态/动态双模式、多文件标签、保存提示
- 静态插件编译进主程序；动态插件可生成多个绘制实体并暴露参数
- `.math` 工程保存/加载

## 构建

需要：

- Visual Studio 2022 C++ 工具链
- CMake 3.20+
- Vulkan SDK

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target VulkanTutorial
```

运行：

```text
build/Release/VulkanTutorial.exe
```

发布包中的程序名为 `MathStudio.exe`。

## 插件

- 静态插件源码：`src/plugins/`
- 动态插件源码：`src/dynamic_plugins/`
- 动态插件 ABI：`src/plugins/plugin_api.h`
- 插件编辑器说明：`docs/插件编辑器使用指南.md`
- 动态插件设计：`docs/动态插件系统设计.md`

## Git 工作流

每次修改后提交：

```powershell
git add -A
git commit -m "描述本次修改"
git push
```

构建目录、运行 DLL、工程文件和可执行文件不会提交。
