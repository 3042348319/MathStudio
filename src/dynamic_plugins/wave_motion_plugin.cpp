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
// 编译产物: WaveMotionPlugin.dll
// 放置位置: MathStudio.exe 旁边的 plugins/
// ====================================================================

#include "plugin_api.h"

#include <cmath>
#include <cstring>
#include <new>

namespace {

constexpr const char* kTypeName = "WaveMotionPlugin";

struct WaveState {
    MsVec3 origin{0.0f, 0.0f, 0.0f};
    float horizontalAmplitude = 0.8f;
    float verticalAmplitude = 0.35f;
    float frequency = 3.0f;
    float speed = 1.4f;
    float phase = 0.0f;
    float time = 0.0f;

    // collect 时复用; 不参与工程持久化。
    MsRenderInstance instances[20]{};
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

/** 构造一个列主序平移+均匀缩放矩阵 */
void makeTransform(MsRenderInstance& item, MsVec3 position, float scale) {
    std::memset(item.transform, 0, sizeof(item.transform));
    item.transform[0] = scale;
    item.transform[5] = scale;
    item.transform[10] = scale;
    item.transform[12] = position.x;
    item.transform[13] = position.y;
    item.transform[14] = position.z;
    item.transform[15] = 1.0f;
}

/**
 * 动态插件绘制多个实体:
 *   * 一个主立方体;
 *   * 后面拖一串逐渐透明、缩小的轨迹立方体。
 *
 * 所有实例都复用宿主 Object 的 Mesh, 通过 transform 生成。
 */
std::size_t pluginCollect(void* instance, const MsObjectApi* api,
                          const MsRenderInstance** out) {
    auto* state = static_cast<WaveState*>(instance);
    if (state == nullptr || api == nullptr || out == nullptr) {
        return 0;
    }

    std::size_t count = 0;

    // 主实体
    makeTransform(state->instances[count],
                  positionAt(*state, state->time), 0.35f);
    state->instances[count].color[0] = 0.72f;
    state->instances[count].color[1] = 0.90f;
    state->instances[count].color[2] = 0.95f;
    state->instances[count].color[3] = 1.0f;
    state->instances[count].transparent = 0;
    ++count;

    // 轨迹实体: 使用过去时间点的位置。
    for (int i = 1; i < 18; ++i) {
        const float fade =
            1.0f - static_cast<float>(i) / 18.0f;
        const float sampleTime =
            state->time - static_cast<float>(i) * 0.055f;
        makeTransform(state->instances[count],
                      positionAt(*state, sampleTime),
                      0.35f * (0.35f + 0.65f * fade));
        state->instances[count].color[0] = 0.72f;
        state->instances[count].color[1] = 0.90f;
        state->instances[count].color[2] = 0.95f;
        state->instances[count].color[3] = 0.10f + 0.45f * fade;
        state->instances[count].transparent = 1;
        ++count;
    }

    *out = state->instances;
    return count;
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
    plugin->collect_instances = &pluginCollect;
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
