// ====================================================================
// sine_motion_plugin.h —— 正方体沿前视图正弦曲线运动
//
// 前视图: 相机从 -Y 看向 +Y, 屏幕水平方向为 X, 垂直方向为 Z。
// 插件保持 Y 不变, 在 XZ 平面内运动:
//
//   x = origin.x + horizontalAmplitude * sin(speed*t + phase)
//   z = origin.z + verticalAmplitude   * sin(frequency*(x-origin.x) + phase)
//
// 因此从正视图看, 物体沿一条正弦曲线往复运动。
// ====================================================================
#pragma once

#include "plugin.h"

#include <glm/glm.hpp>

#include <string>

class SineMotionPlugin : public Plugin {
public:
    SineMotionPlugin(float horizontalAmplitude = 0.8f,
                     float verticalAmplitude = 0.35f,
                     float frequency = 3.0f,
                     float speed = 1.4f,
                     float phase = 0.0f);

    void onAttach(Object& owner) override;
    void update(Object& owner, float deltaSeconds) override;
    void setTime(Object& owner, float time) override;

    std::string name() const override;
    std::string summary() const override;

    void save(BinaryWriter& out) const override;
    bool load(BinaryReader& in) override;

    float horizontalAmplitude() const;
    void setHorizontalAmplitude(float value);

    float verticalAmplitude() const;
    void setVerticalAmplitude(float value);

    float frequency() const;
    void setFrequency(float value);

    float speed() const;
    void setSpeed(float value);

    float phase() const;
    void setPhase(float value);

private:
    glm::vec3 positionAt(float time) const;

    glm::vec3 origin_{0.0f};
    float horizontalAmplitude_ = 0.8f;
    float verticalAmplitude_ = 0.35f;
    float frequency_ = 3.0f;
    float speed_ = 1.4f;
    float phase_ = 0.0f;
    float time_ = 0.0f;
};
