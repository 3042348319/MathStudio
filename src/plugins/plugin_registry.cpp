// ====================================================================
// plugin_registry.cpp
// ====================================================================

#include "plugin_registry.h"

#include <unordered_map>
#include <utility>

namespace {

struct Entry {
    StaticPluginFactory factory;
    std::string sourcePath;
};

std::unordered_map<std::string, Entry>& registry() {
    static std::unordered_map<std::string, Entry> value;
    return value;
}

}  // namespace

bool registerStaticPlugin(const std::string& typeId,
                          StaticPluginFactory factory,
                          const char* sourcePath) {
    if (typeId.empty() || !factory) {
        return false;
    }
    Entry entry;
    entry.factory = std::move(factory);
    if (sourcePath != nullptr) {
        entry.sourcePath = sourcePath;
    }
    registry()[typeId] = std::move(entry);
    return true;
}

std::unique_ptr<Plugin> createRegisteredStaticPlugin(
    const std::string& typeId) {
    const auto& table = registry();
    const auto it = table.find(typeId);
    if (it == table.end() || !it->second.factory) {
        return nullptr;
    }
    return it->second.factory();
}

std::vector<StaticPluginInfo> staticPluginInfos() {
    std::vector<StaticPluginInfo> result;
    result.reserve(registry().size());
    for (const auto& pair : registry()) {
        StaticPluginInfo info;
        info.typeId = pair.first;
        info.sourcePath = pair.second.sourcePath;
        result.push_back(std::move(info));
    }
    return result;
}
