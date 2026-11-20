// ====================================================================
// code_plugin.cpp
// ====================================================================

#include "code_plugin.h"

#include "byte_stream.h"

#include <utility>

CodePlugin::CodePlugin(std::string className, std::string sourceFile)
    : className_(std::move(className)),
      sourceFile_(std::move(sourceFile)) {
}

std::string CodePlugin::name() const {
    return "CodePlugin";
}

std::string CodePlugin::typeId() const {
    return "CodePlugin";
}

std::string CodePlugin::summary() const {
    return "源码插件: " + className_ + " <- " + sourceFile_;
}

void CodePlugin::save(BinaryWriter& out) const {
    out.writeString(className_);
    out.writeString(sourceFile_);
}

bool CodePlugin::load(BinaryReader& in) {
    className_ = in.readString();
    sourceFile_ = in.readString();
    return in.ok();
}

const std::string& CodePlugin::className() const {
    return className_;
}

const std::string& CodePlugin::sourceFile() const {
    return sourceFile_;
}
