// ====================================================================
// parameter_plugin.h —— 通用 key=value 参数插件
//
// 用于给 Object 追加任意参数, 例如:
//   emissionRate = 120
//   gridStep     = 0.25
//   texture      = spark.png
//
// 它不参与运动或实例生成, 只负责保存数据;
// 子窗口浮窗会把 value 显示成可编辑的 InputText。
// ====================================================================
#pragma once

#include "plugin.h"

#include <string>

/**
 * 通用参数插件
 *
 * ParameterPlugin 只实现 Plugin 的 name()/summary(),
 * 其余钩子使用 Plugin 的默认空实现。
 */
class ParameterPlugin : public Plugin {
public:
    /**
     * @param key   参数名 (不要包含 "plugin." 前缀)
     * @param value 参数值, 统一以字符串保存
     */
    ParameterPlugin(std::string key, std::string value);

    // 工程加载器需要先构造空插件, 再调用 load()。
    ParameterPlugin() = default;

    // 参数名, 例如 "emissionRate"
    const std::string& key() const;

    // 参数值, 例如 "120"
    const std::string& value() const;

    // 修改参数值; 浮窗编辑后调用
    void setValue(std::string value);

    // 返回 "ParameterPlugin:<key>", 便于按参数名唯一标识
    std::string name() const override;
    std::string typeId() const override;

    // 返回 "key=value" 形式的一行摘要
    std::string summary() const override;

    void save(BinaryWriter& out) const override;
    bool load(BinaryReader& in) override;

private:
    // 参数名
    std::string key_;

    // 参数值 (字符串形式, 由具体使用方解释)
    std::string value_;
};
