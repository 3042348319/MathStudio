// ====================================================================
// plugin_editor_io.h —— 插件源码路径 / 文本文件工具
// ====================================================================
#pragma once

#include <filesystem>
#include <string>

/** 从当前工作目录向上寻找包含 src/plugins 的项目根目录 */
std::filesystem::path findProjectRoot();

/** 内置插件类型 -> 源码文件 */
std::filesystem::path pluginSourceFile(const std::string& pluginType);

bool readTextFile(const std::filesystem::path& path,
                  std::string& text,
                  std::string& error);

bool writeTextFile(const std::filesystem::path& path,
                   const std::string& text,
                   std::string& error);

/** 去掉非法标识符字符, 生成 C++ 类名 */
std::string sanitizeIdentifier(const std::string& value);

/** 生成新插件头文件/源文件模板 */
std::string makePluginHeaderTemplate(const std::string& className);
std::string makePluginSourceTemplate(const std::string& className,
                                     const std::string& headerFile);

/**
 * 生成动态插件 DLL 的 C ABI 源码模板。
 * 输出都是单文件 .cpp, 由 MathStudioEditorPlugin 目标编译。
 */
std::string makeDynamicPluginSourceTemplate(const std::string& className);
