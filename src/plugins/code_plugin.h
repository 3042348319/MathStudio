// ====================================================================
// code_plugin.h —— 用户插件源码的挂载标记
//
// Ctrl+` 编辑器可以把新建的插件源码文件应用到一个 Object。
// 运行中的程序不能热编译 C++, 所以 CodePlugin 只负责把
// "类名 + 源文件路径" 作为工程资源保存/恢复, 方便下一次编译。
// ====================================================================
#pragma once

#include "plugin.h"

#include <string>

class CodePlugin : public Plugin {
public:
    CodePlugin() = default;
    CodePlugin(std::string className, std::string sourceFile);

    std::string name() const override;
    std::string typeId() const override;
    std::string summary() const override;

    void save(BinaryWriter& out) const override;
    bool load(BinaryReader& in) override;

    const std::string& className() const;
    const std::string& sourceFile() const;

private:
    std::string className_;
    std::string sourceFile_;
};
