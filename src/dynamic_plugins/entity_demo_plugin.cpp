// ====================================================================
// entity_demo_plugin.cpp —— 动态插件生成多个实体示例
//
// 这个插件演示动态插件如何通过 collect_instances 生成实体:
//   * 中央一个立方体;
//   * 外围一圈旋转的彩色立方体;
//   * 所有实体都复用宿主 Object 的 Mesh。
//
// 它不是静态插件, 编译产物是 EntityDemoPlugin.dll。
// ====================================================================

#include "plugin_api.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace {

constexpr const char* kTypeName = "EntityDemoPlugin";
constexpr int kRingCount = 12;

struct State {
    MsVec3 origin{0.0f, 0.0f, 0.0f};
    float time = 0.0f;

    // 可在 Object 参数面板中编辑
    float ringRadius = 1.15f;
    float entityScale = 0.12f;
    float rotationSpeed = 1.2f;
    float verticalWave = 0.12f;

    MsRenderInstance instances[kRingCount + 1]{};
};

void setTransform(MsRenderInstance& item, float x, float y, float z,
                  float scale) {
    std::memset(item.transform, 0, sizeof(item.transform));
    item.transform[0] = scale;
    item.transform[5] = scale;
    item.transform[10] = scale;
    item.transform[12] = x;
    item.transform[13] = y;
    item.transform[14] = z;
    item.transform[15] = 1.0f;
}

const char* pluginType(void*) {
    return kTypeName;
}

void pluginDestroy(void* instance) {
    delete static_cast<State*>(instance);
}

void pluginDestroyPlugin(MsPlugin* plugin) {
    if (plugin == nullptr) {
        return;
    }
    delete static_cast<State*>(plugin->instance);
    delete plugin;
}

void pluginOnAttach(void* instance, const MsObjectApi* api) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr || api == nullptr ||
        api->get_position == nullptr) {
        return;
    }
    state->origin = api->get_position(api->object);
    state->time = 0.0f;
}

void pluginUpdate(void* instance, const MsObjectApi*,
                  float deltaSeconds) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr) {
        return;
    }
    state->time += deltaSeconds;
}

void pluginSetTime(void* instance, const MsObjectApi*,
                   float absoluteTime) {
    auto* state = static_cast<State*>(instance);
    if (state != nullptr) {
        state->time = absoluteTime;
    }
}

std::size_t pluginCollect(void* instance, const MsObjectApi*,
                          const MsRenderInstance** out) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr || out == nullptr) {
        return 0;
    }

    std::size_t count = 0;

    // 中央实体
    setTransform(state->instances[count],
                 state->origin.x, state->origin.y, state->origin.z,
                 0.25f);
    state->instances[count].color[0] = 1.0f;
    state->instances[count].color[1] = 0.62f;
    state->instances[count].color[2] = 0.18f;
    state->instances[count].color[3] = 1.0f;
    state->instances[count].transparent = 0;
    ++count;

    // 外围旋转圆环
    for (int i = 0; i < kRingCount; ++i) {
        const float angle =
            state->time * state->rotationSpeed +
            6.2831853f * static_cast<float>(i) /
                static_cast<float>(kRingCount);
        const float radius = state->ringRadius;
        const float x = state->origin.x + std::cos(angle) * radius;
        const float y = state->origin.y + std::sin(angle) * radius;
        const float z = state->origin.z +
                        state->verticalWave *
                            std::sin(angle * 2.0f + state->time);

        setTransform(state->instances[count], x, y, z,
                     state->entityScale);
        const float t = static_cast<float>(i) /
                        static_cast<float>(kRingCount);
        state->instances[count].color[0] = 0.30f + 0.70f * t;
        state->instances[count].color[1] = 0.55f;
        state->instances[count].color[2] = 1.0f - 0.55f * t;
        state->instances[count].color[3] = 1.0f;
        state->instances[count].transparent = 0;
        ++count;
    }

    *out = state->instances;
    return count;
}

std::size_t parameterCount(void*) {
    return 4;
}

const char* parameterName(void*, std::size_t index) {
    static const char* names[] = {
        "环绕半径", "实体大小", "旋转速度", "垂直波动"
    };
    return index < 4 ? names[index] : nullptr;
}

float parameterGet(void* instance, std::size_t index) {
    const auto* state = static_cast<const State*>(instance);
    if (state == nullptr) {
        return 0.0f;
    }
    switch (index) {
    case 0: return state->ringRadius;
    case 1: return state->entityScale;
    case 2: return state->rotationSpeed;
    case 3: return state->verticalWave;
    default: return 0.0f;
    }
}

void parameterSet(void* instance, std::size_t index, float value) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr) {
        return;
    }
    switch (index) {
    case 0: state->ringRadius = std::clamp(value, 0.1f, 10.0f); break;
    case 1: state->entityScale = std::clamp(value, 0.01f, 2.0f); break;
    case 2: state->rotationSpeed = std::clamp(value, -10.0f, 10.0f); break;
    case 3: state->verticalWave = std::clamp(value, 0.0f, 5.0f); break;
    default: break;
    }
}

std::size_t pluginSave(void* instance, void* buffer,
                       std::size_t capacity) {    const auto* state = static_cast<const State*>(instance);
    if (state == nullptr) {
        return 0;
    }
    if (buffer != nullptr && capacity >= sizeof(State)) {
        std::memcpy(buffer, state, sizeof(State));
    }
    return sizeof(State);
}

int pluginLoad(void* instance, const void* data, std::size_t size) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr || data == nullptr ||
        size != sizeof(State)) {
        return 0;
    }
    std::memcpy(state, data, sizeof(State));
    return 1;
}

MsPlugin* createPlugin() {
    auto* state = new (std::nothrow) State();
    auto* plugin = new (std::nothrow) MsPlugin();
    if (state == nullptr || plugin == nullptr) {
        delete state;
        delete plugin;
        return nullptr;
    }

    plugin->instance = state;
    plugin->type_name = &pluginType;
    plugin->destroy = &pluginDestroy;
    plugin->destroy_plugin = &pluginDestroyPlugin;
    plugin->on_attach = &pluginOnAttach;
    plugin->update = &pluginUpdate;
    plugin->set_time = &pluginSetTime;
    plugin->collect_instances = &pluginCollect;
    plugin->parameter_count = &parameterCount;
    plugin->parameter_name = &parameterName;
    plugin->parameter_get = &parameterGet;
    plugin->parameter_set = &parameterSet;
    plugin->save = &pluginSave;
    plugin->load = &pluginLoad;
    return plugin;
}

}  // namespace

MS_PLUGIN_EXPORT const char* mathstudio_plugin_type(void) {
    return kTypeName;
}

MS_PLUGIN_EXPORT MsPlugin* mathstudio_create_plugin(void) {
    return createPlugin();
}
