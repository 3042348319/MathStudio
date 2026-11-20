// ====================================================================
// dynamic_plugin_manager.cpp
// ====================================================================

#include "dynamic_plugin_manager.h"

#include "byte_stream.h"
#include "object.h"
#include "plugin_api.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace {

/** 把 glm 向量转换为 C ABI 向量 */
MsVec3 toApi(const glm::vec3& value) {
    return MsVec3{value.x, value.y, value.z};
}

MsVec3 objectGetPosition(void* object) {
    const auto* value = static_cast<const Object*>(object);
    return value ? toApi(value->position()) : MsVec3{0.0f, 0.0f, 0.0f};
}

void objectSetPosition(void* object, MsVec3 position) {
    auto* value = static_cast<Object*>(object);
    if (value != nullptr) {
        value->setPosition(glm::vec3(position.x, position.y, position.z));
    }
}

MsVec3 objectGetColor(void* object) {
    const auto* value = static_cast<const Object*>(object);
    if (value == nullptr) {
        return MsVec3{1.0f, 1.0f, 1.0f};
    }
    const glm::vec4 color = value->color();
    return MsVec3{color.r, color.g, color.b};
}

void objectSetColor(void* object, MsVec3 color) {
    auto* value = static_cast<Object*>(object);
    if (value != nullptr) {
        const float alpha = value->color().a;
        value->setColor(glm::vec4(color.x, color.y, color.z, alpha));
    }
}

/** 填好函数表的 Object API */
MsObjectApi makeObjectApi(Object& object) {
    MsObjectApi api{};
    api.object = &object;
    api.get_position = &objectGetPosition;
    api.set_position = &objectSetPosition;
    api.get_color = &objectGetColor;
    api.set_color = &objectSetColor;
    return api;
}

/**
 * 把 DLL 返回的 MsPlugin 包装成主程序的 Plugin。
 *
 * 析构时调用 DLL 自己的 destroy, 因此插件对象的内存
 * 始终由创建它的 DLL 释放。
 */
class DynamicPlugin final : public Plugin {
public:
    DynamicPlugin(MsPlugin* plugin, std::string type)
        : plugin_(plugin), type_(std::move(type)) {
    }

    ~DynamicPlugin() override {
        if (plugin_ != nullptr) {
            if (plugin_->destroy_plugin != nullptr) {
                plugin_->destroy_plugin(plugin_);
            } else {
                if (plugin_->destroy != nullptr) {
                    plugin_->destroy(plugin_->instance);
                }
                delete plugin_;
            }
            plugin_ = nullptr;
        }
    }

    std::string name() const override {
        return type_;
    }

    std::string typeId() const override {
        return type_;
    }

    bool isDynamic() const override {
        return true;
    }

    std::size_t parameterCount() const override {
        if (plugin_ == nullptr ||
            plugin_->parameter_count == nullptr) {
            return 0;
        }
        return plugin_->parameter_count(
            const_cast<void*>(plugin_->instance));
    }

    const char* parameterName(std::size_t index) const override {
        if (plugin_ == nullptr ||
            plugin_->parameter_name == nullptr) {
            return nullptr;
        }
        return plugin_->parameter_name(
            const_cast<void*>(plugin_->instance), index);
    }

    float parameterValue(std::size_t index) const override {
        if (plugin_ == nullptr ||
            plugin_->parameter_get == nullptr) {
            return 0.0f;
        }
        return plugin_->parameter_get(
            const_cast<void*>(plugin_->instance), index);
    }

    void setParameterValue(std::size_t index, float value) override {
        if (plugin_ != nullptr &&
            plugin_->parameter_set != nullptr) {
            plugin_->parameter_set(plugin_->instance, index, value);
        }
    }

    std::string summary() const override {
        return "DynamicPlugin: " + type_;
    }

    void onAttach(Object& owner) override {
        if (plugin_ != nullptr && plugin_->on_attach != nullptr) {
            lastApi_ = makeObjectApi(owner);
            plugin_->on_attach(plugin_->instance, &lastApi_);
        }
    }

    void update(Object& owner, float deltaSeconds) override {
        if (plugin_ != nullptr && plugin_->update != nullptr) {
            lastApi_ = makeObjectApi(owner);
            plugin_->update(plugin_->instance, &lastApi_, deltaSeconds);
        }
    }

    void setTime(Object& owner, float time) override {
        if (plugin_ != nullptr && plugin_->set_time != nullptr) {
            lastApi_ = makeObjectApi(owner);
            plugin_->set_time(plugin_->instance, &lastApi_, time);
        }
    }

    bool collectInstances(
        const Object& owner,
        std::vector<ObjectInstance>& out) const override {
        if (plugin_ == nullptr ||
            plugin_->collect_instances == nullptr) {
            return false;
        }

        // collect 回调只读, 但 Object API 需要 Object* 句柄。
        // 这里通过 const_cast 复用同一组回调, 插件不应在 collect
        // 中修改对象。
        MsObjectApi api = makeObjectApi(const_cast<Object&>(owner));
        const MsRenderInstance* instances = nullptr;
        const std::size_t count = plugin_->collect_instances(
            plugin_->instance, &api, &instances);
        if (count == 0 || instances == nullptr) {
            return false;
        }

        const std::size_t capped = std::min<std::size_t>(count, 4096);
        out.reserve(out.size() + capped);
        for (std::size_t i = 0; i < capped; ++i) {
            const MsRenderInstance& source = instances[i];
            glm::mat4 transform(1.0f);
            std::memcpy(&transform[0][0], source.transform,
                        sizeof(source.transform));
            out.push_back({
                &owner.mesh(),
                transform,
                glm::vec4(source.color[0], source.color[1],
                          source.color[2], source.color[3]),
                source.transparent != 0,
            });
        }
        return true;
    }

    void save(BinaryWriter& out) const override {
        if (plugin_ == nullptr || plugin_->save == nullptr) {
            out.writeU32(0);
            return;
        }

        const std::size_t needed =
            plugin_->save(plugin_->instance, nullptr, 0);
        if (needed == 0 || needed > 16u * 1024u * 1024u) {
            out.writeU32(0);
            return;
        }

        std::vector<std::uint8_t> buffer(needed);
        const std::size_t written = plugin_->save(
            plugin_->instance, buffer.data(), buffer.size());
        const std::uint32_t stored = static_cast<std::uint32_t>(
            std::min(written, buffer.size()));
        out.writeU32(stored);
        out.data().insert(out.data().end(),
                          buffer.begin(),
                          buffer.begin() + stored);
    }

    bool load(BinaryReader& in) override {
        const std::uint32_t size = in.readU32();
        if (!in.ok()) {
            return false;
        }
        if (size == 0) {
            return true;
        }
        if (size > 16u * 1024u * 1024u) {
            return false;
        }

        std::vector<std::uint8_t> buffer(size);
        if (!in.readBytes(buffer.data(), buffer.size())) {
            return false;
        }

        if (plugin_ == nullptr || plugin_->load == nullptr) {
            return true;
        }
        return plugin_->load(plugin_->instance, buffer.data(),
                             buffer.size()) != 0 && in.ok();
    }

private:
    MsPlugin* plugin_ = nullptr;
    std::string type_;
    MsObjectApi lastApi_{};
};

std::filesystem::path executableDirectory() {
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer,
        static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    if (length == 0) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
}

}  // namespace

DynamicPluginManager& DynamicPluginManager::instance() {
    static DynamicPluginManager manager;
    return manager;
}

std::size_t DynamicPluginManager::discover(
    const std::filesystem::path& directory, std::string& error) {
    std::error_code filesystemError;
    if (!std::filesystem::exists(directory, filesystemError) ||
        filesystemError) {
        error = "插件目录不存在";
        return 0;
    }

    std::size_t loaded = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(directory, filesystemError)) {
        if (filesystemError) {
            error = "读取插件目录失败";
            return loaded;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".dll") {
            continue;
        }

        HMODULE module = LoadLibraryW(entry.path().wstring().c_str());
        if (module == nullptr) {
            continue;
        }

        const auto typeFn = reinterpret_cast<MsPluginTypeFn>(
            GetProcAddress(module, MS_PLUGIN_TYPE_SYMBOL));
        const auto createFn = reinterpret_cast<MsCreatePluginFn>(
            GetProcAddress(module, MS_PLUGIN_CREATE_SYMBOL));
        if (typeFn == nullptr || createFn == nullptr) {
            FreeLibrary(module);
            continue;
        }

        const char* typeName = typeFn();
        if (typeName == nullptr || std::strlen(typeName) == 0) {
            FreeLibrary(module);
            continue;
        }

        const std::string type(typeName);
        bool duplicate = false;
        for (auto& existing : entries_) {
            if (existing.first == type) {
                // 编辑器重新编译后会出现同名类型的新 DLL。
                // 旧 DLL 仍可能被旧插件实例使用, 所以不 FreeLibrary,
                // 只把创建入口替换为新 DLL。
                existing.second.module = module;
                existing.second.create =
                    reinterpret_cast<void*>(createFn);
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        entries_.push_back(
            {type, Entry{module, reinterpret_cast<void*>(createFn)}});
        ++loaded;
    }

    error.clear();
    return loaded;
}

std::size_t DynamicPluginManager::discoverDefault(std::string& error) {
    const std::filesystem::path executable = executableDirectory();
    const std::filesystem::path current = std::filesystem::current_path();

    const std::filesystem::path candidates[] = {
        executable / "plugins",
        current / "plugins",
        current / ".." / "plugins",
    };

    std::size_t total = 0;
    std::string lastError;
    for (const auto& candidate : candidates) {
        std::string candidateError;
        total += discover(candidate, candidateError);
        if (!candidateError.empty()) {
            lastError = candidateError;
        }
    }
    if (total == 0 && !lastError.empty()) {
        error = lastError;
    } else {
        error.clear();
    }
    return total;
}

std::unique_ptr<Plugin> DynamicPluginManager::create(
    const std::string& type) {
    for (const auto& entry : entries_) {
        if (entry.first != type) {
            continue;
        }

        const auto createFn =
            reinterpret_cast<MsCreatePluginFn>(entry.second.create);
        MsPlugin* plugin = createFn ? createFn() : nullptr;
        if (plugin == nullptr) {
            return nullptr;
        }
        return std::make_unique<DynamicPlugin>(plugin, type);
    }
    return nullptr;
}

std::vector<std::string> DynamicPluginManager::types() const {
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) {
        result.push_back(entry.first);
    }
    return result;
}



