// ====================================================================
// wave_motion_plugin.cpp —— 动态插件示例 (DLL)
//
// 这个文件不链接 MathStudio 主程序, 只 include plugin_api.h。
// 它演示了动态插件的最小完整实现:
//   * 读取/写入 Object 位置;
//   * on_attach 记录初始位置;
//   * update / set_time 计算正弦运动;
//   * save / load 序列化插件状态;
//   * 导出 type + create 两个 C 函数。
//
// 编译产物: EditorPlugin.dll
// 放置位置: MathStudio.exe 旁边的 plugins/
// ====================================================================

#include "plugin_api.h"

#include <cmath>
#include <cstring>
#include <new>

namespace {

constexpr const char* kTypeName = "EditorPlugin";

struct WaveState {
    MsVec3 origin{0.0f, 0.0f, 0.0f};
    float horizontalAmplitude = 0.8f;
    float verticalAmplitude = 0.35f;
    float frequency = 3.0f;
    float speed = 1.4f;
    float phase = 0.0f;
    float time = 0.0f;
};

MsVec3 positionAt(const WaveState& state, float time) {
    const float x = state.origin.x +
                    state.horizontalAmplitude *
                        std::sin(state.speed * time + state.phase);
    const float z = state.origin.z +
                    state.verticalAmplitude *
                        std::sin(state.frequency * (x - state.origin.x) +
                                 state.phase);
    return MsVec3{x, state.origin.y, z};
}

const char* pluginType(void*) {
    return kTypeName;
}

void pluginDestroy(void* instance) {
    delete static_cast<WaveState*>(instance);
}

/** 在 DLL 内部释放整个插件对象, 避免跨 CRT delete */
void pluginDestroyPlugin(MsPlugin* plugin) {
    if (plugin == nullptr) {
        return;
    }
    delete static_cast<WaveState*>(plugin->instance);
    delete plugin;
}

void pluginOnAttach(void* instance, const MsObjectApi* api) {
    auto* state = static_cast<WaveState*>(instance);
    if (state == nullptr || api == nullptr ||
        api->get_position == nullptr) {
        return;
    }
    state->origin = api->get_position(api->object);
    state->time = 0.0f;
    if (api->set_position != nullptr) {
        api->set_position(api->object, positionAt(*state, 0.0f));
    }
}

void pluginUpdate(void* instance, const MsObjectApi* api,
                  float deltaSeconds) {
    auto* state = static_cast<WaveState*>(instance);
    if (state == nullptr || api == nullptr ||
        api->set_position == nullptr) {
        return;
    }
    state->time += deltaSeconds;
    api->set_position(api->object, positionAt(*state, state->time));
}

void pluginSetTime(void* instance, const MsObjectApi* api,
                   float absoluteTime) {
    auto* state = static_cast<WaveState*>(instance);
    if (state == nullptr || api == nullptr ||
        api->set_position == nullptr) {
        return;
    }
    state->time = absoluteTime;
    api->set_position(api->object, positionAt(*state, absoluteTime));
}

std::size_t pluginSave(void* instance, void* buffer, std::size_t capacity) {
    const auto* state = static_cast<const WaveState*>(instance);
    if (state == nullptr) {
        return 0;
    }
    if (buffer != nullptr && capacity >= sizeof(WaveState)) {
        std::memcpy(buffer, state, sizeof(WaveState));
    }
    return sizeof(WaveState);
}

int pluginLoad(void* instance, const void* data, std::size_t size) {
    auto* state = static_cast<WaveState*>(instance);
    if (state == nullptr || data == nullptr ||
        size != sizeof(WaveState)) {
        return 0;
    }
    std::memcpy(state, data, sizeof(WaveState));
    return 1;
}

/** 把回调表填好, 返回给宿主 */
MsPlugin* createPlugin() {
    auto* state = new (std::nothrow) WaveState();
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
    plugin->save = &pluginSave;
    plugin->load = &pluginLoad;
    return plugin;
}

}  // namespace

// DLL 扫描阶段只调用 type, 不创建实例。
MS_PLUGIN_EXPORT const char* mathstudio_plugin_type(void) {
    return kTypeName;
}

// 工程加载 / 手动挂载时调用 create。
MS_PLUGIN_EXPORT MsPlugin* mathstudio_create_plugin(void) {
    return createPlugin();
}

