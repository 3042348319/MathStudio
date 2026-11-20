# MathStudio

MathStudio 是一个使用 **C++17 + Vulkan + GLFW + GLM + Dear ImGui** 编写的 3D 数学场景编辑器。

项目重点：

- Vulkan 实时 3D 视口；
- 对象、组别、可见性管理；
- 关键帧/时间轴；
- 正交标准视图；
- 可扩展的静态 C++ 插件系统；
- 可热扫描的动态 DLL 插件系统；
- 内置多文件插件代码编辑器；
- `.math` 工程保存/加载。

---

## 目录

- [1. 功能概览](#1-功能概览)
- [2. 构建与运行](#2-构建与运行)
- [3. 界面与基础操作](#3-界面与基础操作)
- [4. 插件系统总览](#4-插件系统总览)
- [5. 插件代码编辑器](#5-插件代码编辑器)
- [6. 动态插件 ABI](#6-动态插件-abi)
- [7. 静态插件](#7-静态插件)
- [8. 工程文件](#8-工程文件)
- [9. 快捷键](#9-快捷键)
- [10. 目录结构](#10-目录结构)
- [11. 常见问题](#11-常见问题)
- [12. Git 工作流](#12-git-工作流)

---

## 1. 功能概览

### 场景与对象

- 立方体、球体、圆柱体、圆锥体、平面；
- 坐标系对象；
- OBJ 模型导入；
- 对象位置、颜色、名称修改；
- 对象分组、拖动到组；
- 对象显示/隐藏眼睛按钮；
- 对象插件参数面板；
- 工程级撤销历史记录。

### 视图

- 透视 3D 视口；
- 小键盘标准视图：
  - `1` 前视图
  - `2` 后视图
  - `3` 上视图
  - `7` 下视图
  - `8` 左视图
  - `9` 右视图
- 标准视图使用正交投影；
- `Esc` 退出正交视图并恢复透视相机；
- 中键拖动旋转；
- 右键拖动平移；
- 滚轮缩放。

### 时间轴

- `T` 显示/隐藏时间轴；
- 播放/暂停；
- 时间轴拖动；
- 插件按绝对时间恢复位置。

### 插件

- 内置静态插件；
- 用户创建的静态插件；
- 动态 DLL 插件；
- 插件参数自动面板；
- 动态插件生成多个绘制实体；
- 插件代码编辑器双模式；
- 静态插件编译并自动替换可执行文件。

---

## 2. 构建与运行

### 环境

- Windows 10/11
- Visual Studio 2022，安装 **使用 C++ 的桌面开发**
- CMake 3.20+
- Vulkan SDK
- Git（可选）

### 配置

```powershell
cmake -S . -B build -A x64
```

如果 Vulkan SDK 没有自动找到：

```powershell
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.341.1"
cmake -S . -B build -A x64
```

### 编译

```powershell
cmake --build build --config Release --target VulkanTutorial
```

输出：

```text
build/Release/VulkanTutorial.exe
```

发布时通常改名为：

```text
MathStudio.exe
```

### 运行

```powershell
.\build\Release\VulkanTutorial.exe
```

程序需要和以下目录一起运行：

```text
MathStudio.exe
shaders/
resources/
plugins/
```

`plugins/` 不存在时会自动创建。

---

## 3. 界面与基础操作

### 3.1 工程窗口

每个 `.math` 工程对应一个独立 GLFW 窗口。

顶部菜单：

```text
File    Object    Setting
```

File：

- 新建工程
- 打开工程
- 保存
- 另存为
- 关闭工程

Object：

- 坐标系
- 正方体
- 圆柱体
- 圆锥体
- 球体
- 平面
- 正弦运动立方体
- 动态波浪立方体
- 动态实体演示

Setting：

- 设置快捷键和 UI 细节

### 3.2 对象资源

浮动面板里的“对象资源”会显示：

```text
组别
  └── 对象
```

每个对象前面的眼睛图标：

```text
睁眼：显示
闭眼：隐藏
```

点击眼睛切换可见性。

隐藏后：

- 不参与 3D 绘制；
- 仍在对象列表中；
- 仍可选中、改名、分组；
- 工程保存后保持隐藏状态。

对象右键菜单支持：

- 重命名；
- 移动到组；
- 删除。

对象也可以直接拖动到组节点。

### 3.3 对象参数

选中对象后，参数面板可修改：

- 名称；
- 坐标 X/Y/Z；
- 颜色；
- 插件参数。

### 3.4 时间轴

按 `T` 显示时间轴。

时间轴支持：

- 播放；
- 暂停；
- 拖动；
- 按绝对时间定位。

插件通过 `update()` 和 `setTime()` 响应时间。

### 3.5 相机

- 中键拖动：轨道旋转；
- 右键拖动：平移；
- 滚轮：推近/拉远；
- 小键盘标准视图；
- `Esc` 退出正交视图。

---

## 4. 插件系统总览

MathStudio 有两类插件。

| 项目 | 静态插件 | 动态插件 |
|---|---|---|
| 编译产物 | 主程序 exe 的一部分 | 独立 DLL |
| 源码目录 | `src/plugins/` | `src/dynamic_plugins/` |
| 是否需要重启 | 编译静态插件后自动重启 | 通常需要重启才能替换 DLL |
| 能否实时扫描 | 不能 | 可以 |
| 能否生成多个实体 | 可以 | 可以（复用 Object Mesh） |
| 能否自定义新 Mesh | 可以 | 暂不支持 |
| 能否暴露参数 | 可以 | 可以 |
| 能否热卸载代码 | 不能 | DLL 已加载后不能安全热卸载 |

### 静态插件

静态插件编译进 exe，适合：

- 自定义网格；
- 粒子轨迹；
- 坐标系；
- 复杂渲染实例；
- 需要完整 C++ 接口的功能。

### 动态插件

动态插件是 DLL，适合：

- 运动、颜色、时间驱动；
- 多个实例复用现有 Mesh；
- 参数可在运行时调整；
- 插件代码独立分发。

动态插件通过 `plugin_api.h` 的 C ABI 与主程序通信。

---

## 5. 插件代码编辑器

### 5.1 打开编辑器

默认快捷键：

```text
Ctrl+`
```

编辑器是独立浮动窗口，可以拖动边缘调整大小。

### 5.2 双模式

编辑器右上角有一个模式按钮，用于切换：

```text
动态模式
静态模式
```

#### 动态模式

显示：

- 对象选择列表；
- 当前对象已挂载的动态插件；
- 动态插件源码标签；
- 动态插件创建；
- 编译并加载；
- 扫描动态插件；
- 应用到当前 Object。

不显示：

- 静态插件列表；
- 创建静态插件；
- 编译静态插件；
- 静态插件源码标签。

#### 静态模式

不显示对象列表，只显示：

- 已经编译进当前程序的静态插件列表；
- 静态插件源码标签；
- 新建静态插件；
- 保存；
- 编译静态插件；
- 应用到当前 Object（使用主面板当前选中的对象）。

静态插件列表包括：

```text
ParticlePlugin
SineParticlePlugin
CoordinateSystemPlugin
ParameterPlugin
SineMotionPlugin
```

以及用户编译进 exe 的自定义静态插件。

点击列表项会打开对应的 `.cpp` 源码。

### 5.3 多文件标签

编辑器支持同时打开多个源码文件：

- 浏览器式标签栏；
- 标签可拖动排序；
- 标签标题带 `*` 表示未保存；
- 关闭单个标签时提示保存；
- 关闭整个编辑器时提示保存全部；
- 切换标签不会弹保存提示；
- 不同模式的标签不会混在一起显示。

### 5.4 新建静态插件

静态模式中：

```text
类名：MyPlugin
文件名：my_plugin
    ↓
＋ 创建静态插件
```

创建时：

- 只创建内存中的未保存标签，不立即写磁盘；
- 生成 `.h` 和 `.cpp` 两个标签；
- 点击保存后才真正生成文件；
- 只有磁盘上已存在同名 `.h/.cpp` 时才提示覆盖。

静态模板会自动注册：

```cpp
registerStaticPlugin("MyPlugin", [] {
    return std::make_unique<MyPlugin>();
}, __FILE__);
```

因此编译进 exe 后不需要手动修改 `plugin_factory.cpp`。

### 5.5 新建动态插件

动态模式中：

```text
类名：MyPlugin
文件名：my_plugin
    ↓
＋ 创建动态插件
```

动态插件实际构建源文件固定为：

```text
src/dynamic_plugins/editor_plugin.cpp
```

创建时同样先放到未保存标签。

只有以下情况会提示同名：

- 已加载动态 DLL 中存在同名类型；
- `editor_plugin.cpp` 中的 `kTypeName` 已经是该类型名。

### 5.6 保存

```text
↓ 保存
```

保存当前标签。

如果文件是新创建的，会写入：

- 静态插件：`src/plugins/`
- 动态插件：`src/dynamic_plugins/`

### 5.7 编译并加载动态插件

动态模式下点击：

```text
编译并加载
```

流程：

```text
保存当前动态源码
    ↓
调用 CMake 构建 MathStudioEditorPlugin
    ↓
复制 DLL 到 plugins/
    ↓
重新扫描动态插件
    ↓
按类名创建插件
    ↓
挂载到当前 Object
```

成功状态会显示在编辑器底部。

### 5.8 编译静态插件

静态模式下点击：

```text
编译静态插件
```

流程：

```text
保存所有源码
    ↓
配置 build_static
    ↓
编译 VulkanTutorial
    ↓
生成新的可执行文件
    ↓
关闭当前 MathStudio
    ↓
替换原 MathStudio.exe
    ↓
自动重新启动
```

注意：

- 编译前应先保存 `.math` 工程；
- 程序会自动关闭并重启；
- 重启后静态插件才会出现在静态插件列表；
- 原来的静态插件源码会保留；
- 如果编译失败，编辑器会显示 `static_build.log` 末尾内容。

### 5.9 应用插件到对象

动态模式下：

```text
选择对象
→ 选择插件
→ 应用到当前 Object
```

静态模式下：

```text
在主面板选中对象
→ 在静态插件列表找到插件
→ 应用到当前 Object
```

如果当前程序没有该静态插件类，会提示：

```text
当前程序中没有这个静态插件
```

### 5.10 关闭提示

关闭单个未保存标签：

```text
关闭文件
当前文件有未保存修改。

[保存并关闭] [不保存] [取消]
```

关闭编辑器：

```text
未保存的源码
是否保存所有修改？

[保存全部] [不保存] [取消]
```

关闭工程：

- 先检查未保存源码；
- 再检查工程是否有修改；
- 分别提示保存。

### 5.11 完整流程：动态插件

```text
打开工程
  ↓
Ctrl+`
  ↓
动态模式
  ↓
＋ 创建动态插件
  ↓
编辑 editor_plugin.cpp
  ↓
保存
  ↓
编译并加载
  ↓
调整参数 / 时间轴
  ↓
保存工程
```

### 5.12 完整流程：静态插件

```text
打开工程
  ↓
Ctrl+`
  ↓
静态模式
  ↓
＋ 创建静态插件
  ↓
编辑 .h / .cpp
  ↓
保存
  ↓
编译静态插件
  ↓
程序自动替换并重启
  ↓
静态模式中看到新插件
  ↓
在主面板选中对象
  ↓
应用到当前 Object
  ↓
保存工程
```

---

## 6. 动态插件 ABI

动态插件只依赖：

```text
src/plugins/plugin_api.h
```

不依赖主程序 C++ 类型。

### 6.1 导出函数

```c
MS_PLUGIN_EXPORT const char* mathstudio_plugin_type(void);
MS_PLUGIN_EXPORT MsPlugin* mathstudio_create_plugin(void);
```

### 6.2 MsPlugin

`MsPlugin` 包含：

```c
void* instance;
const char* (*type_name)(void*);
void (*destroy)(void*);
void (*destroy_plugin)(MsPlugin*);
void (*on_attach)(void*, const MsObjectApi*);
void (*update)(void*, const MsObjectApi*, float);
void (*set_time)(void*, const MsObjectApi*, float);
size_t (*collect_instances)(void*, const MsObjectApi*,
                            const MsRenderInstance**);
size_t (*parameter_count)(void*);
const char* (*parameter_name)(void*, size_t);
float (*parameter_get)(void*, size_t);
void (*parameter_set)(void*, size_t, float);
size_t (*save)(void*, void*, size_t);
int (*load)(void*, const void*, size_t);
```

### 6.3 多实体

```c
typedef struct MsRenderInstance {
    float transform[16]; // 列主序
    float color[4];
    int transparent;
} MsRenderInstance;
```

插件返回实例数组，宿主用当前 Object 的 Mesh 绘制多个实体。

示例：

```text
EntityDemoPlugin
    1 个中央立方体
    12 个外围旋转立方体
    4 个可调参数
```

### 6.4 参数

插件实现：

```c
parameter_count
parameter_name
parameter_get
parameter_set
```

宿主参数面板会自动生成 `DragFloat`，插件不需要链接 ImGui。

如果这些回调为 `NULL`，插件参数面板为空。

### 6.5 动态插件限制

- 不能上传全新 Mesh；
- 不能使用主程序内部 C++ 类型；
- 已加载 DLL 不能安全热卸载；
- 同名 DLL 不能覆盖运行中的文件；
- 不支持自定义 ImGui 参数面板；
- 所有实例复用 Object 的 Mesh。

---

## 7. 静态插件

### 7.1 源码

静态插件位于：

```text
src/plugins/
```

典型文件：

```text
my_plugin.h
my_plugin.cpp
```

### 7.2 自注册

模板生成：

```cpp
namespace {
const bool g_registered = registerStaticPlugin(
    "MyPlugin", [] {
        return std::make_unique<MyPlugin>();
    }, __FILE__);
}
```

`plugin_factory` 创建顺序：

```text
内置插件
    ↓
静态插件注册表
    ↓
动态插件
```

### 7.3 完整 C++ 接口

静态插件可以继承完整 `Plugin`：

```cpp
class MyPlugin : public Plugin {
public:
    void update(Object&, float) override;
    bool collectInstances(const Object&,
                          std::vector<ObjectInstance>&) const override;
    void save(BinaryWriter&) const override;
    bool load(BinaryReader&) override;
};
```

因此可以：

- 使用任意 `Mesh`；
- 生成多个 `ObjectInstance`；
- 使用自定义变换和颜色；
- 生成粒子、轨迹、坐标轴等。

---

## 8. 工程文件

工程是二进制 `.math` 文件。

保存内容：

- 工程名；
- 相机；
- 对象名称；
- 对象位置；
- 对象颜色；
- 对象可见性；
- Mesh 顶点/索引；
- 插件类型和插件状态；
- 组别；
- 未分组对象。

工程保存使用版本标记；旧工程没有可见性字段时默认显示。

---

## 9. 快捷键

| 功能 | 默认快捷键 |
|---|---|
| 新建工程 | `Ctrl+N` |
| 打开工程 | `Ctrl+O` |
| 保存 | `Ctrl+S` |
| 另存为 | `Ctrl+Shift+S` |
| 关闭工程 | `Ctrl+W` |
| 设置 | `Ctrl+,` |
| 插件代码编辑器 | Ctrl + 反引号键 |
| 时间轴 | `T` |
| 退出正交视图 | `Esc` |
| 前视图 | 小键盘 `1` |
| 后视图 | 小键盘 `2` |
| 上视图 | 小键盘 `3` |
| 下视图 | 小键盘 `7` |
| 左视图 | 小键盘 `8` |
| 右视图 | 小键盘 `9` |

快捷键可在 `Setting → 设置...` 中修改。

---

## 10. 目录结构

```text
MathStudio/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── PROGRESS.md
│   ├── 插件编辑器使用指南.md
│   └── 动态插件系统设计.md
├── resources/
│   ├── icons/
│   └── models/
├── shaders/
├── src/
│   ├── core/
│   ├── io/
│   ├── render/
│   ├── ui/
│   ├── plugins/
│   ├── dynamic_plugins/
│   ├── main.cpp
│   ├── window.cpp
│   └── window.h
├── third_party/
│   ├── glfw/
│   ├── glm/
│   └── imgui/
├── plugins/          # 运行时生成，不提交
├── projects/         # 运行时工程，不提交
├── build/            # 构建目录，不提交
└── build_static/     # 静态插件构建目录，不提交
```

---

## 11. 常见问题

### 11.1 编辑器里看不到动态插件源码

需要为该动态插件类型配置源码映射，例如：

```cpp
if (pluginType == "EntityDemoPlugin") {
    return "../dynamic_plugins/entity_demo_plugin.cpp";
}
```

否则只知道 DLL 类型，不知道对应 `.cpp`。

### 11.2 静态插件列表为空

静态模式只显示：

- 已编译进当前 exe 的自定义静态插件；
- 内置静态插件。

刚编译完需要等待程序自动重启。

### 11.3 静态插件编译失败

查看：

```text
build_static/static_build.log
```

常见原因：

- 缺少 `#include "byte_stream.h"`；
- `registerStaticPlugin` 返回值错误；
- 源码没有保存；
- 工程目录配置错误。

### 11.4 动态插件没有参数

插件没有实现：

```text
parameter_count
parameter_name
parameter_get
parameter_set
```

这四个回调都为空时，参数面板就是空的。

### 11.5 动态插件不生成实体

检查：

- `collect_instances` 是否返回大于 0；
- 实例 Mesh 是否使用当前 Object 的 Mesh；
- 变换矩阵是否为列主序；
- 插件是否真的挂载到对象。

### 11.6 编译静态插件后程序自动关闭

这是预期行为。

Windows 不能覆盖正在运行的 exe，所以：

```text
构建新 exe
→ 等待当前进程退出
→ 替换原 exe
→ 重新启动
```

编译前请保存工程。

---

## 12. Git 工作流

本项目已经使用 Git 管理。

每次修改后：

```powershell
git add -A
git commit -m "描述本次修改"
git push
```

不提交：

- `build/`
- `build_static/`
- `plugins/`
- `projects/`
- `*.exe`
- `*.log`
- `*.zip`

仓库：

https://github.com/3042348319/MathStudio

---

## 相关文档

- [插件编辑器使用指南](docs/插件编辑器使用指南.md)
- [动态插件系统设计](docs/动态插件系统设计.md)

