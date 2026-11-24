# ====================================================================
# Vulkan 完整示例 —— CMake 构建脚本
# 参照教程: https://docs.vulkan.net.cn/tutorial/latest/00_Introduction.html
# 功能: 编译 GLSL 着色器为 SPIR-V, 生成 Windows 可执行程序
# ====================================================================

# 要求 CMake 3.20 以上 (3.20 起 FindVulkan 提供 Vulkan::glslc 目标)
cmake_minimum_required(VERSION 3.20)

# 项目名称, 仅使用 C++ 语言
project(VulkanTutorial LANGUAGES CXX)

# ---- 1. C++ 标准设置 ----
# 使用 C++17 (代码中用到了 std::optional, std::chrono 等特性)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 2. 查找 Vulkan SDK ----
# 依赖环境变量 VULKAN_SDK (LunarG 安装器会自动设置)
# 成功后会提供目标 Vulkan::Vulkan (头文件包含路径 + vulkan-1.lib 链接库)
find_package(Vulkan REQUIRED)

# ---- 3. GLFW 窗口库 ----
# 优先使用项目内 third_party/glfw, 这样不依赖本机 vcpkg 安装路径。
# 如果本地依赖不存在, 再回退到 find_package(glfw3)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include/GLFW/glfw3.h")
    add_library(glfw SHARED IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/bin/glfw3.dll"
        IMPORTED_IMPLIB
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib/glfw3dll.lib"
        INTERFACE_INCLUDE_DIRECTORIES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include"
    )
else()
    find_package(glfw3 3.3 REQUIRED)
endif()

# ---- 4. GLM 数学库 (纯头文件) ----
# 同样优先使用 third_party/glm 中的头文件。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm/glm/glm.hpp")
    add_library(glm::glm INTERFACE IMPORTED)
    set_target_properties(glm::glm PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm"
    )
else()
    find_package(glm REQUIRED)
endif()

# ---- 5. 查找着色器编译器 glslc (随 Vulkan SDK 提供) ----
# glslc 负责把 GLSL 源代码编译成 SPIR-V 字节码 (.spv 文件)
# 优先使用 CMake 自带的 Vulkan::glslc 目标, 找不到就到 SDK 目录里找
if(TARGET Vulkan::glslc)
    set(GLSLC Vulkan::glslc)
else()
    find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin")
endif()
if(NOT GLSLC)
    message(FATAL_ERROR "找不到 glslc! 请安装 Vulkan SDK 并设置 VULKAN_SDK 环境变量")
endif()

# ---- 6. 定义可执行目标 ----
# WIN32 表示这是 Windows GUI 程序 (不额外弹出黑色控制台窗口)
#
# 目录结构:
#   src/main.cpp          : 程序入口
#   src/window.*          : 主窗口与 Vulkan 初始化
#   src/core/             : Mesh / Object / Plugin 基础接口
#   src/plugins/          : 具体插件 (Particle / CoordinateSystem / Parameter)
#   src/render/           : SubWindow / ViewportRenderer / ShaderLoader
#   src/ui/               : ImGui 菜单
#   third_party/imgui     : Dear ImGui 核心 + GLFW/Vulkan 后端
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

# 插件目录使用 glob: Ctrl+` 编辑器新建的 .cpp 会在下次 CMake
# 重新配置时自动加入工程, 不需要手动修改源文件列表。
file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${SRC_DIR}/plugins/*.cpp")
file(GLOB PLUGIN_HEADERS CONFIGURE_DEPENDS
    "${SRC_DIR}/plugins/*.h")

add_executable(${PROJECT_NAME} WIN32
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/window.cpp
    ${SRC_DIR}/window.h

    # Windows 图标资源 (MathStudio.ico)
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/MathStudio.rc"

    ${SRC_DIR}/core/mesh.cpp
    ${SRC_DIR}/core/mesh.h
    ${SRC_DIR}/core/object.cpp
    ${SRC_DIR}/core/object.h
    ${SRC_DIR}/core/object_id.h
    ${SRC_DIR}/core/object_hierarchy.cpp
    ${SRC_DIR}/core/object_hierarchy.h
    ${SRC_DIR}/core/object_instance.h
    ${SRC_DIR}/core/plugin.h
    ${SRC_DIR}/plugins/plugin_api.h
    ${SRC_DIR}/plugins/dynamic_plugin_manager.cpp
    ${SRC_DIR}/plugins/dynamic_plugin_manager.h

    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}

    ${SRC_DIR}/io/byte_stream.h
    ${SRC_DIR}/io/compression.cpp
    ${SRC_DIR}/io/compression.h
    ${SRC_DIR}/io/project_io.cpp
    ${SRC_DIR}/io/project_io.h
    ${SRC_DIR}/io/project_history.cpp
    ${SRC_DIR}/io/project_history.h
    ${SRC_DIR}/io/plugin_editor_io.cpp
    ${SRC_DIR}/io/plugin_editor_io.h

    ${SRC_DIR}/render/shader_loader.cpp
    ${SRC_DIR}/render/shader_loader.h
    ${SRC_DIR}/render/sub_window.cpp
    ${SRC_DIR}/render/sub_window.h
    ${SRC_DIR}/render/viewport_context.h
    ${SRC_DIR}/render/viewport_renderer.cpp
    ${SRC_DIR}/render/viewport_renderer.h

    ${SRC_DIR}/ui/menu.cpp
    ${SRC_DIR}/ui/menu.h
    ${SRC_DIR}/ui/file_dialog.cpp
    ${SRC_DIR}/ui/file_dialog.h
    ${SRC_DIR}/ui/app_icon.cpp
    ${SRC_DIR}/ui/app_icon.h
    ${SRC_DIR}/ui/settings.cpp
    ${SRC_DIR}/ui/settings.h
    ${SRC_DIR}/ui/theme.cpp
    ${SRC_DIR}/ui/theme.h

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${SRC_DIR}"
    "${SRC_DIR}/core"
    "${SRC_DIR}/plugins"
    "${SRC_DIR}/dynamic_plugins"
    "${SRC_DIR}/render"
    "${SRC_DIR}/ui"
    "${SRC_DIR}/io"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

# --------------------------------------------------------------------
# 动态插件示例: 单独的 DLL, 不链接主程序
# --------------------------------------------------------------------
add_library(WaveMotionPlugin SHARED
    "${SRC_DIR}/dynamic_plugins/wave_motion_plugin.cpp"
    "${SRC_DIR}/plugins/plugin_api.h"
)
target_include_directories(WaveMotionPlugin PRIVATE
    "${SRC_DIR}/plugins"
)
if(MSVC)
    target_compile_options(WaveMotionPlugin PRIVATE /utf-8)
endif()

# 把 DLL 放到可执行文件旁边的 plugins/ 目录。
add_custom_command(TARGET WaveMotionPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:WaveMotionPlugin>"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/WaveMotionPlugin.dll"
    COMMENT "复制 WaveMotionPlugin.dll 到 plugins/"
)
add_dependencies(${PROJECT_NAME} WaveMotionPlugin)

# --------------------------------------------------------------------
# 动态多实体示例: EntityDemoPlugin
# --------------------------------------------------------------------
add_library(EntityDemoPlugin SHARED
    "${SRC_DIR}/dynamic_plugins/entity_demo_plugin.cpp"
    "${SRC_DIR}/plugins/plugin_api.h"
)
target_include_directories(EntityDemoPlugin PRIVATE
    "${SRC_DIR}/plugins"
)
if(MSVC)
    target_compile_options(EntityDemoPlugin PRIVATE /utf-8)
endif()
add_custom_command(TARGET EntityDemoPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:EntityDemoPlugin>"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/EntityDemoPlugin.dll"
    COMMENT "复制 EntityDemoPlugin.dll 到 plugins/"
)
add_dependencies(${PROJECT_NAME} EntityDemoPlugin)

# --------------------------------------------------------------------
# 传递函数极坐标曲线动态插件
# --------------------------------------------------------------------
add_library(TransferFunctionPlugin SHARED
    "${SRC_DIR}/dynamic_plugins/transfer_function_plugin.cpp"
    "${SRC_DIR}/plugins/plugin_api.h"
)
target_include_directories(TransferFunctionPlugin PRIVATE
    "${SRC_DIR}/plugins"
)
if(MSVC)
    target_compile_options(TransferFunctionPlugin PRIVATE /utf-8)
endif()
add_custom_command(TARGET TransferFunctionPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:TransferFunctionPlugin>"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/TransferFunctionPlugin.dll"
    COMMENT "复制 TransferFunctionPlugin.dll 到 plugins/"
)
add_dependencies(${PROJECT_NAME} TransferFunctionPlugin)

# --------------------------------------------------------------------
# 编辑器“编译并加载”使用的动态插件目标。
#
# 这个目标不随主程序默认构建 (EXCLUDE_FROM_ALL), 只有用户在
# 插件编辑器里点击“编译并加载”时才执行:
#   cmake --build ... --target MathStudioEditorPlugin
#
# 编辑器把当前源码写入 src/dynamic_plugins/editor_plugin.cpp,
# 构建产物再由宿主复制到 plugins/ 下加载。
# --------------------------------------------------------------------
add_library(MathStudioEditorPlugin SHARED EXCLUDE_FROM_ALL
    "${SRC_DIR}/dynamic_plugins/editor_plugin.cpp"
    "${SRC_DIR}/plugins/plugin_api.h"
)
target_include_directories(MathStudioEditorPlugin PRIVATE
    "${SRC_DIR}/plugins"
)
if(MSVC)
    target_compile_options(MathStudioEditorPlugin PRIVATE /utf-8)
endif()

# 把本地 GLFW 运行库复制到可执行文件旁边, 双击 exe 也能启动。
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/bin/glfw3.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/glfw3.dll"
    COMMENT "复制 glfw3.dll 到输出目录"
)

# ---- 7. 编译 GLSL 着色器为 SPIR-V 字节码 ----
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders") # 源码目录中的着色器
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders") # 编译产物 .spv 输出目录

# 顶点着色器: shaders/shader.vert -> build/shaders/vert.spv
# add_custom_command: 声明一条自定义编译规则, 输入变化时自动重新编译
add_custom_command(
    OUTPUT "${SHADER_OUTPUT_DIR}/vert.spv"                              # 输出文件
    COMMAND ${GLSLC} "${SHADER_SOURCE_DIR}/shader.vert" -o "${SHADER_OUTPUT_DIR}/vert.spv"
    DEPENDS "${SHADER_SOURCE_DIR}/shader.vert"                          # 依赖的源文件
    VERBATIM                                                            # 原样传递命令行参数
    COMMENT "编译顶点着色器 shader.vert -> vert.spv"
)

# 片元着色器: shaders/shader.frag -> build/shaders/frag.spv
add_custom_command(
    OUTPUT "${SHADER_OUTPUT_DIR}/frag.spv"
    COMMAND ${GLSLC} "${SHADER_SOURCE_DIR}/shader.frag" -o "${SHADER_OUTPUT_DIR}/frag.spv"
    DEPENDS "${SHADER_SOURCE_DIR}/shader.frag"
    VERBATIM
    COMMENT "编译片元着色器 shader.frag -> frag.spv"
)

# 子窗口对象顶点着色器: shaders/object.vert -> build/shaders/object_vert.spv
add_custom_command(
    OUTPUT "${SHADER_OUTPUT_DIR}/object_vert.spv"
    COMMAND ${GLSLC} "${SHADER_SOURCE_DIR}/object.vert" -o "${SHADER_OUTPUT_DIR}/object_vert.spv"
    DEPENDS "${SHADER_SOURCE_DIR}/object.vert"
    VERBATIM
    COMMENT "编译子窗口顶点着色器 object.vert -> object_vert.spv"
)

# 子窗口对象片元着色器: shaders/object.frag -> build/shaders/object_frag.spv
add_custom_command(
    OUTPUT "${SHADER_OUTPUT_DIR}/object_frag.spv"
    COMMAND ${GLSLC} "${SHADER_SOURCE_DIR}/object.frag" -o "${SHADER_OUTPUT_DIR}/object_frag.spv"
    DEPENDS "${SHADER_SOURCE_DIR}/object.frag"
    VERBATIM
    COMMENT "编译子窗口片元着色器 object.frag -> object_frag.spv"
)

# 无限网格地板顶点着色器
add_custom_command(
    OUTPUT "${SHADER_OUTPUT_DIR}/grid_vert.spv"
    COMMAND ${GLSLC} "${SHADER_SOURCE_DIR}/grid.vert" -o "${SHADER_OUTPUT_DIR}/grid_vert.spv"
    DEPENDS "${SHADER_SOURCE_DIR}/grid.vert"
    VERBATIM
    COMMENT "编译网格地板顶点着色器 grid.vert -> grid_vert.spv"
)

# 无限网格地板片元着色器
add_custom_command(
    OUTPUT "${SHADER_OUTPUT_DIR}/grid_frag.spv"
    COMMAND ${GLSLC} "${SHADER_SOURCE_DIR}/grid.frag" -o "${SHADER_OUTPUT_DIR}/grid_frag.spv"
    DEPENDS "${SHADER_SOURCE_DIR}/grid.frag"
    VERBATIM
    COMMENT "编译网格地板片元着色器 grid.frag -> grid_frag.spv"
)

# 自定义目标 Shaders: ALL 表示默认构建时都会执行 (而非按需)
add_custom_target(Shaders ALL DEPENDS
    "${SHADER_OUTPUT_DIR}/vert.spv"
    "${SHADER_OUTPUT_DIR}/frag.spv"
    "${SHADER_OUTPUT_DIR}/object_vert.spv"
    "${SHADER_OUTPUT_DIR}/object_frag.spv"
    "${SHADER_OUTPUT_DIR}/grid_vert.spv"
    "${SHADER_OUTPUT_DIR}/grid_frag.spv"
)
# 让可执行程序依赖着色器编译结果
add_dependencies(${PROJECT_NAME} Shaders)

# ---- 7.5 拷贝模型资源 ----
# 运行目录下的 resources/models/*.mesh 供 Mesh::loadFromResource 查找
add_custom_target(Resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "${CMAKE_CURRENT_BINARY_DIR}/resources"
    COMMENT "拷贝模型资源 resources -> build/resources"
)
add_dependencies(${PROJECT_NAME} Resources)

# ---- 8. 链接依赖库 ----
# glfw          : 窗口管理 (优先使用 third_party/glfw)
# glm::glm      : 数学库 (INTERFACE 目标, 纯头文件)
# Vulkan::Vulkan: Vulkan 加载库 vulkan-1.lib
# user32/gdi32  : Win32 窗口/输入相关符号
# comdlg32      : 导入 OBJ 的 GetOpenFileNameW 文件选择框
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw glm::glm Vulkan::Vulkan user32 gdi32 comdlg32 dwmapi)

# 源码使用 UTF-8 (中文 UI 文本), 让 MSVC 按 UTF-8 解释字符串字面量
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    # MSVC 下 WIN32 子系统默认查找 WinMain 入口函数,
    # 显式指定 mainCRTStartup, 让普通的 main() 也能用于 GUI 程序 (不弹控制台窗口)
    target_link_options(${PROJECT_NAME} PRIVATE /ENTRY:mainCRTStartup)
endif()

# ---- 9. Visual Studio 调试配置 ----
# 程序通过相对路径查找着色器文件 (vert.spv / frag.spv)
# 把 VS 调试器的工作目录设为着色器输出目录, 在 VS 里按 F5 即可直接运行
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${SHADER_OUTPUT_DIR}")
