// ====================================================================
// plugin_editor_io.cpp
// ====================================================================

#include "plugin_editor_io.h"

#include <cctype>
#include <fstream>
#include <sstream>

std::filesystem::path findProjectRoot() {
    std::error_code error;
    std::filesystem::path current =
        std::filesystem::current_path(error);
    if (error) {
        return std::filesystem::path();
    }

    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(
                current / "src" / "plugins", error)) {
            return current;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::path();
}

std::filesystem::path pluginSourceFile(const std::string& pluginType) {
    const std::string fileName = [&pluginType]() -> std::string {
        if (pluginType == "ParticlePlugin") {
            return "particle_plugin.cpp";
        }
        if (pluginType == "SineParticlePlugin") {
            return "sine_particle_plugin.cpp";
        }
        if (pluginType == "SineMotionPlugin") {
            return "sine_motion_plugin.cpp";
        }
        if (pluginType == "WaveMotionPlugin") {
            return "../dynamic_plugins/wave_motion_plugin.cpp";
        }
        if (pluginType == "EntityDemoPlugin") {
            return "../dynamic_plugins/entity_demo_plugin.cpp";
        }
        if (pluginType == "CoordinateSystemPlugin") {
            return "coordinate_system_plugin.cpp";
        }
        if (pluginType == "ParameterPlugin") {
            return "parameter_plugin.cpp";
        }
        return std::string();
    }();

    const std::filesystem::path root = findProjectRoot();
    if (fileName.empty() || root.empty()) {
        return std::filesystem::path();
    }
    return root / "src" / "plugins" / fileName;
}

bool readTextFile(const std::filesystem::path& path,
                  std::string& text,
                  std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "无法打开文件: " + path.string();
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

bool writeTextFile(const std::filesystem::path& path,
                   const std::string& text,
                   std::string& error) {
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(),
                                        filesystemError);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "无法写入文件: " + path.string();
        return false;
    }

    file << text;
    if (!file.good()) {
        error = "写文件失败: " + path.string();
        return false;
    }
    return true;
}

std::string sanitizeIdentifier(const std::string& value) {
    std::string result;
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_') {
            result.push_back(ch);
        } else if (std::isspace(uch)) {
            result.push_back('_');
        }
    }

    if (result.empty()) {
        result = "Custom";
    }
    if (std::isdigit(static_cast<unsigned char>(result.front()))) {
        result.insert(result.begin(), '_');
    }
    return result;
}

std::string makePluginHeaderTemplate(const std::string& className) {
    std::ostringstream out;
    out << "// ====================================================================\n"
        << "// " << className << ".h —— 自定义插件\n"
        << "// ====================================================================\n"
        << "#pragma once\n\n"
        << "#include \"plugin.h\"\n\n"
        << "#include <string>\n\n"
        << "class " << className << " : public Plugin {\n"
        << "public:\n"
        << "    std::string name() const override;\n"
        << "    std::string summary() const override;\n\n"
        << "    void update(Object& owner, float deltaSeconds) override;\n"
        << "    bool collectInstances(const Object& owner,\n"
        << "                          std::vector<ObjectInstance>& out) const override;\n\n"
        << "    void save(BinaryWriter& out) const override;\n"
        << "    bool load(BinaryReader& in) override;\n"
        << "};\n";
    return out.str();
}

std::string makePluginSourceTemplate(const std::string& className,
                                     const std::string& headerFile) {
    std::ostringstream out;
    out << "// ====================================================================\n"
        << "// " << className << ".cpp —— 自定义插件实现\n"
        << "// ====================================================================\n"
        << "#include \"" << headerFile << "\"\n"
        << "#include \"object.h\"\n"
        << "#include \"byte_stream.h\"\n"
        << "#include \"plugin_registry.h\"\n\n"
        << "#include <memory>\n\n"
        << "namespace {\n"
        << "const bool g_registered = registerStaticPlugin(\n"
        << "    \"" << className << "\", [] {\n"
        << "        return std::make_unique<" << className << ">();\n"
        << "    }, __FILE__);\n"
        << "}  // namespace\n\n"
        << "std::string " << className << "::name() const {\n"
        << "    return \"" << className << "\";\n"
        << "}\n\n"
        << "std::string " << className << "::summary() const {\n"
        << "    return \"" << className << "\";\n"
        << "}\n\n"
        << "void " << className << "::update(Object&, float) {\n"
        << "    // 在这里更新对象状态。\n"
        << "}\n\n"
        << "bool " << className << "::collectInstances(\n"
        << "    const Object&, std::vector<ObjectInstance>&) const {\n"
        << "    // 返回 false 表示使用 Object 的默认网格。\n"
        << "    return false;\n"
        << "}\n\n"
        << "void " << className << "::save(BinaryWriter&) const {\n"
        << "}\n\n"
        << "bool " << className << "::load(BinaryReader& in) {\n"
        << "    return in.ok();\n"
        << "}\n";
    return out.str();
}

/**
 * 动态插件模板。
 *
 * 与静态模板不同, 它只 include plugin_api.h, 不 include object.h。
 * 编译目标是独立 DLL; 代码里所有 Object 访问都通过 MsObjectApi
 * 函数表完成, 因此不会依赖主程序的 C++ 类布局。
 *
 * 生成的类名同时作为 DLL 的 type_name 和工程中的 typeId。
 */
std::string makeDynamicPluginSourceTemplate(const std::string& className) {
    std::ostringstream out;
    out << "// ====================================================================\n"
        << "// " << className << ".cpp —— 动态插件 (C ABI DLL)\n"
        << "//\n"
        << "// 这个文件由 MathStudio 插件编辑器生成。\n"
        << "// 它只依赖 plugin_api.h, 不依赖主程序的 C++ 类型。\n"
        << "// ====================================================================\n\n"
        << "#include \"plugin_api.h\"\n\n"
        << "#include <cmath>\n"
        << "#include <cstring>\n"
        << "#include <new>\n\n"
        << "namespace {\n\n"
        << "constexpr const char* kTypeName = \"" << className << "\";\n\n"
        << "struct State {\n"
        << "    MsVec3 origin{0.0f, 0.0f, 0.0f};\n"
        << "    float amplitude = 0.8f;\n"
        << "    float frequency = 3.0f;\n"
        << "    float speed = 1.4f;\n"
        << "    float time = 0.0f;\n"
        << "};\n\n"
        << "MsVec3 positionAt(const State& s, float t) {\n"
        << "    const float x = s.origin.x +\n"
        << "        s.amplitude * std::sin(s.speed * t);\n"
        << "    const float z = s.origin.z +\n"
        << "        s.amplitude * 0.45f * std::sin(s.frequency *\n"
        << "            (x - s.origin.x));\n"
        << "    return MsVec3{x, s.origin.y, z};\n"
        << "}\n\n"
        << "const char* typeName(void*) { return kTypeName; }\n\n"
        << "void destroyInstance(void* p) {\n"
        << "    delete static_cast<State*>(p);\n"
        << "}\n\n"
        << "void destroyPlugin(MsPlugin* p) {\n"
        << "    if (!p) return;\n"
        << "    delete static_cast<State*>(p->instance);\n"
        << "    delete p;\n"
        << "}\n\n"
        << "void onAttach(void* p, const MsObjectApi* api) {\n"
        << "    auto* s = static_cast<State*>(p);\n"
        << "    if (!s || !api || !api->get_position) return;\n"
        << "    s->origin = api->get_position(api->object);\n"
        << "    s->time = 0.0f;\n"
        << "    if (api->set_position)\n"
        << "        api->set_position(api->object, positionAt(*s, 0.0f));\n"
        << "}\n\n"
        << "void update(void* p, const MsObjectApi* api, float dt) {\n"
        << "    auto* s = static_cast<State*>(p);\n"
        << "    if (!s || !api || !api->set_position) return;\n"
        << "    s->time += dt;\n"
        << "    api->set_position(api->object, positionAt(*s, s->time));\n"
        << "}\n\n"
        << "void setTime(void* p, const MsObjectApi* api, float t) {\n"
        << "    auto* s = static_cast<State*>(p);\n"
        << "    if (!s || !api || !api->set_position) return;\n"
        << "    s->time = t;\n"
        << "    api->set_position(api->object, positionAt(*s, t));\n"
        << "}\n\n"
        << "size_t saveState(void* p, void* buffer, size_t capacity) {\n"
        << "    auto* s = static_cast<State*>(p);\n"
        << "    if (!s) return 0;\n"
        << "    if (buffer && capacity >= sizeof(State))\n"
        << "        std::memcpy(buffer, s, sizeof(State));\n"
        << "    return sizeof(State);\n"
        << "}\n\n"
        << "int loadState(void* p, const void* data, size_t size) {\n"
        << "    auto* s = static_cast<State*>(p);\n"
        << "    if (!s || !data || size != sizeof(State)) return 0;\n"
        << "    std::memcpy(s, data, sizeof(State));\n"
        << "    return 1;\n"
        << "}\n\n"
        << "}  // namespace\n\n"
        << "MS_PLUGIN_EXPORT const char* mathstudio_plugin_type(void) {\n"
        << "    return kTypeName;\n"
        << "}\n\n"
        << "MS_PLUGIN_EXPORT MsPlugin* mathstudio_create_plugin(void) {\n"
        << "    auto* state = new (std::nothrow) State();\n"
        << "    auto* plugin = new (std::nothrow) MsPlugin();\n"
        << "    if (!state || !plugin) {\n"
        << "        delete state;\n"
        << "        delete plugin;\n"
        << "        return nullptr;\n"
        << "    }\n"
        << "    plugin->instance = state;\n"
        << "    plugin->type_name = &typeName;\n"
        << "    plugin->destroy = &destroyInstance;\n"
        << "    plugin->destroy_plugin = &destroyPlugin;\n"
        << "    plugin->on_attach = &onAttach;\n"
        << "    plugin->update = &update;\n"
        << "    plugin->set_time = &setTime;\n"
        << "    plugin->save = &saveState;\n"
        << "    plugin->load = &loadState;\n"
        << "    return plugin;\n"
        << "}\n";
    return out.str();
}
