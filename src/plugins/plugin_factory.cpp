// ====================================================================
// plugin_factory.cpp —— 内置插件工厂
// ====================================================================

#include "plugin_factory.h"

#include "code_plugin.h"
#include "coordinate_system_plugin.h"
#include "dynamic_plugin_manager.h"
#include "parameter_plugin.h"
#include "particle_plugin.h"
#include "plugin_registry.h"
#include "sine_motion_plugin.h"
#include "sine_particle_plugin.h"

std::unique_ptr<Plugin> createPluginByType(const std::string& type) {
    if (type == "ParticlePlugin") {
        return std::make_unique<ParticlePlugin>();
    }
    if (type == "SineParticlePlugin") {
        return std::make_unique<SineParticlePlugin>();
    }
    if (type == "SineMotionPlugin") {
        return std::make_unique<SineMotionPlugin>();
    }
    if (type == "CoordinateSystemPlugin") {
        return std::make_unique<CoordinateSystemPlugin>();
    }
    if (type == "ParameterPlugin") {
        return std::make_unique<ParameterPlugin>("", "");
    }
    if (type == "CodePlugin") {
        return std::make_unique<CodePlugin>();
    }

    // 自定义静态插件在编译进 exe 后通过自注册表提供工厂。
    if (auto staticPlugin = createRegisteredStaticPlugin(type)) {
        return staticPlugin;
    }

    // 最后尝试动态插件目录中已经扫描到的类型。
    return DynamicPluginManager::instance().create(type);
}
