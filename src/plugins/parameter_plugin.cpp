// ====================================================================
// parameter_plugin.cpp —— 通用参数插件实现
// ====================================================================

#include "parameter_plugin.h"

#include "byte_stream.h"

#include <utility>

// --------------------------------------------------------------------
// 构造函数
//
// 使用 std::move 把参数直接移动到成员中, 避免多一次字符串拷贝。
// --------------------------------------------------------------------
ParameterPlugin::ParameterPlugin(std::string key, std::string value)
    : key_(std::move(key)),
      value_(std::move(value)) {
}

// --------------------------------------------------------------------
// 读取参数名
// --------------------------------------------------------------------
const std::string& ParameterPlugin::key() const {
    return key_;
}

// --------------------------------------------------------------------
// 读取参数值
// --------------------------------------------------------------------
const std::string& ParameterPlugin::value() const {
    return value_;
}

// --------------------------------------------------------------------
// 修改参数值
// --------------------------------------------------------------------
void ParameterPlugin::setValue(std::string value) {
    value_ = std::move(value);
}

// --------------------------------------------------------------------
// 插件名称
//
// 加上 key 是为了让同一个 Object 上的多个 ParameterPlugin
// 拥有不同的名字, 便于按名字查找/删除。
// --------------------------------------------------------------------
std::string ParameterPlugin::name() const {
    return "ParameterPlugin:" + key_;
}

std::string ParameterPlugin::typeId() const {
    return "ParameterPlugin";
}

// --------------------------------------------------------------------
// 一行摘要
//
// 例如 key_="emissionRate", value_="120"
// 返回 "emissionRate = 120"。
// --------------------------------------------------------------------
std::string ParameterPlugin::summary() const {
    return key_ + " = " + value_;
}

void ParameterPlugin::save(BinaryWriter& out) const {
    out.writeString(key_);
    out.writeString(value_);
}

bool ParameterPlugin::load(BinaryReader& in) {
    key_ = in.readString();
    value_ = in.readString();
    return in.ok();
}
