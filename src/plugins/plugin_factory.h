// ====================================================================
// plugin_factory.h —— 按 .math 文件中的类型名创建插件
// ====================================================================
#pragma once

#include <memory>
#include <string>

class Plugin;

/** 类型名不存在时返回 nullptr */
std::unique_ptr<Plugin> createPluginByType(const std::string& type);
