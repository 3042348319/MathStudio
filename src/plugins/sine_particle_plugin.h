// ====================================================================
// sine_particle_plugin.h —— 沿 XZ 平面正弦曲线运动的粒子插件
//
// 这个插件同时负责两件事:
//   1. 让粒子沿一条 XZ 平面上的正弦曲线来回运动;
//   2. 在粒子后方生成一条透明发光轨迹。
//
// 轨迹颜色基于粒子本身的颜色:
//   * RGB 提亮 (更"发光");
//   * 越靠后的点 alpha 越低 (更透明)。
//
// 所有轨迹点都是确定性计算出来的, 因此时间轴可以任意向前/向后拖动。
// ====================================================================
#pragma once

#include "plugin.h"

#include <glm/glm.hpp>

#include <string>

class SineParticlePlugin : public Plugin {
public:
    explicit SineParticlePlugin(
        float amplitude = 0.35f,       // 正弦曲线振幅 (Z 方向)
        float pathLength = 2.4f,       // X 方向往返长度
        float frequency = 2.4f,        // 正弦空间频率
        float speed = 1.0471976f,      // 时间速度 (12 秒两个完整周期)
        float phase = 0.0f,            // 初始相位
        float size = 0.10f,            // 粒子尺寸
        float trailDuration = 1.2f,    // 轨迹保留时间 (秒)
        int trailSamples = 96,         // 轨迹采样点数
        float trailPointSize = 0.05f   // 轨迹点尺寸
    );

    void onAttach(Object& owner) override;
    void update(Object& owner, float deltaSeconds) override;
    void setTime(Object& owner, float time) override;

    bool collectInstances(const Object& owner,
                          std::vector<ObjectInstance>& out) const override;

    std::string name() const override;
    std::string summary() const override;

    void save(BinaryWriter& out) const override;
    bool load(BinaryReader& in) override;

    // ---- 参数读写 (浮窗/主菜单使用) ----

    float amplitude() const;
    void setAmplitude(float value);

    float pathLength() const;
    void setPathLength(float value);

    float frequency() const;
    void setFrequency(float value);

    float speed() const;
    void setSpeed(float value);

    float phase() const;
    void setPhase(float value);

    float size() const;
    void setSize(float value);

    float trailDuration() const;
    void setTrailDuration(float value);

    int trailSamples() const;
    void setTrailSamples(int value);

    float trailPointSize() const;
    void setTrailPointSize(float value);

private:
    // 根据绝对时间计算曲线上的位置
    glm::vec3 positionAt(float time) const;

    // 粒子出生点 (onAttach 时记录)
    glm::vec3 origin_{0.0f};

    // 曲线参数
    float amplitude_ = 0.35f;
    float pathLength_ = 2.4f;
    float frequency_ = 2.4f;
    float speed_ = 1.0471976f;
    float phase_ = 0.0f;

    // 主粒子尺寸
    float size_ = 0.10f;

    // 轨迹参数
    float trailDuration_ = 1.2f;
    int trailSamples_ = 96;
    float trailPointSize_ = 0.05f;

    // 当前动画时间
    float time_ = 0.0f;
};
