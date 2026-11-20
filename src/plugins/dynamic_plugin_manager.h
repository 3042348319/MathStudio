// ====================================================================
// dynamic_plugin_manager.h —— 动态插件扫描与加载
//
// 主程序启动时扫描 plugins/*.dll, 读取 DLL 导出的类型名,
// 把 type -> (module, create function) 记录到注册表。
//
// 项目加载时, createPluginByType("WaveMotionPlugin") 会先查内置
// 插件, 再查这张动态插件表。
// ====================================================================
#pragma once

#include "plugin.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class DynamicPluginManager {
public:
    static DynamicPluginManager& instance();

    DynamicPluginManager(const DynamicPluginManager&) = delete;
    DynamicPluginManager& operator=(const DynamicPluginManager&) = delete;

    /**
     * 扫描一个目录下的所有 .dll。
     * @return 成功加载的 DLL 数量
     */
    std::size_t discover(const std::filesystem::path& directory,
                         std::string& error);

    /** 扫描 exe 旁边的 plugins、当前目录 plugins 等常见位置 */
    std::size_t discoverDefault(std::string& error);

    /** 按类型创建动态插件; 未注册时返回 nullptr */
    std::unique_ptr<Plugin> create(const std::string& type);

    /** 已注册的动态插件类型, 供设置/编辑器显示 */
    std::vector<std::string> types() const;

private:
    DynamicPluginManager() = default;

    struct Entry {
        void* module = nullptr;   // HMODULE; 保持 DLL 生命周期
        void* create = nullptr;   // MsCreatePluginFn
    };

    // 只保存不透明指针, 头文件不依赖 windows.h。
    std::vector<std::pair<std::string, Entry>> entries_;
};
