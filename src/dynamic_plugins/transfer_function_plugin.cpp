// ====================================================================
// transfer_function_plugin.cpp —— 传递函数极坐标曲线插件
//
// 传递函数:
//
//              b0 + b1*s
//   G(s) = ------------------
//           a0 + a1*s + a2*s^2
//
// 令 s = j*w, 扫描 w in [wMin, wMax]:
//
//   Re = Re(G(jw))
//   Im = Im(G(jw))
//   r  = |G(jw)|
//   θ  = arg(G(jw))
//
// 在 X-Z 平面上绘制:
//
//   X = r*cos(θ) = Re
//   Z = r*sin(θ) = Im
//
// 一个立方体沿这条曲线运动; collect_instances 同时绘制整条曲线。
//
// 参数都可以在 Object 参数面板中调整, 不需要重新编译。
// ====================================================================

#include "plugin_api.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <new>

namespace {

constexpr const char* kTypeName = "TransferFunctionPlugin";
constexpr double kMinFrequency = 0.03;
constexpr int kCurveSamples = 128;

struct State {
    MsVec3 origin{0.0f, 0.0f, 0.0f};
    float time = 0.0f;

    // G(s) 的系数
    float b0 = 1.0f;
    float b1 = 0.4f;
    float a0 = 1.0f;
    float a1 = 0.8f;
    float a2 = 1.0f;

    // 扫描/绘制参数
    float maxFrequency = 8.0f;
    float sweepSpeed = 0.45f;
    float curveScale = 2.0f;

    MsRenderInstance instances[kCurveSamples + 1]{};
};

std::complex<double> evaluateTransfer(const State& state,
                                      double frequency) {
    const std::complex<double> s(0.0, frequency);
    const std::complex<double> numerator =
        static_cast<double>(state.b0) +
        static_cast<double>(state.b1) * s;
    const std::complex<double> denominator =
        static_cast<double>(state.a0) +
        static_cast<double>(state.a1) * s +
        static_cast<double>(state.a2) * s * s;

    if (std::abs(denominator) < 1e-9) {
        return {0.0, 0.0};
    }
    return numerator / denominator;
}

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

/** 在第 index 个采样点计算世界坐标 */
MsVec3 curvePoint(const State& state, int index) {
    const double t = kCurveSamples <= 1
                         ? 0.0
                         : static_cast<double>(index) /
                               static_cast<double>(kCurveSamples - 1);
    const double minFrequency = kMinFrequency;
    const double maxFrequency =
        std::max(static_cast<double>(state.maxFrequency),
                 kMinFrequency * 2.0);
    const double frequency =
        minFrequency * std::pow(maxFrequency / minFrequency, t);

    const std::complex<double> value =
        evaluateTransfer(state, frequency);
    const double magnitude = std::clamp(
        std::abs(value),
        0.0, 6.0);
    const double angle = std::arg(value);

    const double localX =
        magnitude * std::cos(angle) *
        static_cast<double>(state.curveScale);
    const double localZ =
        magnitude * std::sin(angle) *
        static_cast<double>(state.curveScale);

    return {state.origin.x + static_cast<float>(localX),
            state.origin.y,
            state.origin.z + static_cast<float>(localZ)};
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

void pluginUpdate(void* instance, const MsObjectApi* api,
                  float deltaSeconds) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr) {
        return;
    }
    state->time += deltaSeconds;

    if (api != nullptr && api->set_position != nullptr) {
        const float phase =
            std::fmod(state->time * state->sweepSpeed, 1.0f);
        const int index = static_cast<int>(
            phase * static_cast<float>(kCurveSamples - 1));
        const MsVec3 position =
            curvePoint(*state, std::clamp(index, 0,
                                         kCurveSamples - 1));
        api->set_position(api->object, position);
    }
}

void pluginSetTime(void* instance, const MsObjectApi* api,
                   float absoluteTime) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr) {
        return;
    }
    state->time = absoluteTime;
    pluginUpdate(instance, api, 0.0f);
}

std::size_t pluginCollect(void* instance, const MsObjectApi*,
                          const MsRenderInstance** out) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr || out == nullptr) {
        return 0;
    }

    std::size_t count = 0;

    // 整条传递函数极坐标曲线
    for (int i = 0; i < kCurveSamples; ++i) {
        const MsVec3 point = curvePoint(*state, i);
        setTransform(state->instances[count], point.x, point.y,
                     point.z, 0.022f);
        const float t = static_cast<float>(i) /
                        static_cast<float>(kCurveSamples - 1);
        state->instances[count].color[0] = 0.22f + 0.65f * t;
        state->instances[count].color[1] = 0.55f + 0.25f * t;
        state->instances[count].color[2] = 1.0f - 0.55f * t;
        state->instances[count].color[3] = 0.85f;
        state->instances[count].transparent = 1;
        ++count;
    }

    // 沿曲线运动的标记实体
    const float phase =
        std::fmod(state->time * state->sweepSpeed, 1.0f);
    const int markerIndex = static_cast<int>(
        phase * static_cast<float>(kCurveSamples - 1));
    const MsVec3 marker =
        curvePoint(*state, std::clamp(markerIndex, 0,
                                     kCurveSamples - 1));
    setTransform(state->instances[count], marker.x, marker.y,
                 marker.z, 0.18f);
    state->instances[count].color[0] = 1.0f;
    state->instances[count].color[1] = 0.52f;
    state->instances[count].color[2] = 0.12f;
    state->instances[count].color[3] = 1.0f;
    state->instances[count].transparent = 0;
    ++count;

    *out = state->instances;
    return count;
}

std::size_t parameterCount(void*) {
    return 8;
}

const char* parameterName(void*, std::size_t index) {
    static const char* names[] = {
        "b0", "b1", "a0", "a1", "a2",
        "频率上限", "动画速度", "曲线缩放"
    };
    return index < 8 ? names[index] : nullptr;
}

float parameterGet(void* instance, std::size_t index) {
    const auto* state = static_cast<const State*>(instance);
    if (state == nullptr) {
        return 0.0f;
    }
    switch (index) {
    case 0: return state->b0;
    case 1: return state->b1;
    case 2: return state->a0;
    case 3: return state->a1;
    case 4: return state->a2;
    case 5: return state->maxFrequency;
    case 6: return state->sweepSpeed;
    case 7: return state->curveScale;
    default: return 0.0f;
    }
}

void parameterSet(void* instance, std::size_t index, float value) {
    auto* state = static_cast<State*>(instance);
    if (state == nullptr) {
        return;
    }
    switch (index) {
    case 0: state->b0 = std::clamp(value, -20.0f, 20.0f); break;
    case 1: state->b1 = std::clamp(value, -20.0f, 20.0f); break;
    case 2: state->a0 = std::clamp(value, -20.0f, 20.0f); break;
    case 3: state->a1 = std::clamp(value, -20.0f, 20.0f); break;
    case 4: state->a2 = std::clamp(value, -20.0f, 20.0f); break;
    case 5: state->maxFrequency = std::clamp(value, 0.1f, 100.0f); break;
    case 6: state->sweepSpeed = std::clamp(value, -5.0f, 5.0f); break;
    case 7: state->curveScale = std::clamp(value, 0.05f, 10.0f); break;
    default: break;
    }
}

std::size_t pluginSave(void* instance, void* buffer,
                       std::size_t capacity) {
    const auto* state = static_cast<const State*>(instance);
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
