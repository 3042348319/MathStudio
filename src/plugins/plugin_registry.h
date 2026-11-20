// ====================================================================
// plugin_registry.h —— 静态插件自注册表
//
// 自定义静态插件编译进 MathStudio.exe 后, 如何让 plugin_factory
// 按类名字符串创建它？
//
// 之前必须手动修改 plugin_factory.cpp 增加 if 分支。
// 现在静态插件源码在自己的 .cpp 里注册:
//
//   registerStaticPlugin("MyPlugin", [] {
//       return std::make_unique<MyPlugin>();
//   });
//
// 插件工厂先查内置类型, 再查动态 DLL, 最后查这张静态注册表。
// ====================================================================
#pragma once

#include "plugin.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

using StaticPluginFactory =
    std::function<std::unique_ptr<Plugin>()>;

struct StaticPluginInfo {
    std::string typeId;
    std::string sourcePath;
};

/** 注册一个静态插件类型 (通常在插件 .cpp 的匿名命名空间中调用)
 *
 * sourcePath 可传 __FILE__, 插件编辑器用它定位源码。
 */
bool registerStaticPlugin(const std::string& typeId,
                          StaticPluginFactory factory,
                          const char* sourcePath = nullptr);

/** 按类型创建已经注册的静态插件; 未注册返回 nullptr */
std::unique_ptr<Plugin> createRegisteredStaticPlugin(
    const std::string& typeId);

/** 当前程序内已注册的静态插件列表 (用于静态插件编辑器) */
std::vector<StaticPluginInfo> staticPluginInfos();
