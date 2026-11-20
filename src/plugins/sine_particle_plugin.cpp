// ====================================================================
// sine_particle_plugin.cpp —— XZ 平面正弦曲线粒子与透明轨迹
// ====================================================================

#include "sine_particle_plugin.h"

#include "byte_stream.h"
#include "object.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

SineParticlePlugin::SineParticlePlugin(float amplitude,
                                       float pathLength,
                                       float frequency,
                                       float speed,
                                       float phase,
                                       float size,
                                       float trailDuration,
                                       int trailSamples,
                                       float trailPointSize)
    : amplitude_(std::max(0.0f, amplitude)),
      pathLength_(std::max(0.01f, pathLength)),
      frequency_(frequency),
      speed_(speed),
      phase_(phase),
      size_(std::max(0.0f, size)),
      trailDuration_(std::max(0.0f, trailDuration)),
      trailSamples_(std::clamp(trailSamples, 2, 160)),
      trailPointSize_(std::max(0.001f, trailPointSize)) {
}

// --------------------------------------------------------------------
// 曲线位置
//
// X 方向来回摆动:
//   x = origin.x + pathLength/2 * sin(speed*t + phase)
//
// Z 方向是 X 的正弦函数:
//   z = origin.z + amplitude * sin(frequency*(x-origin.x) + phase)
//
// 因此粒子的轨迹严格位于 XZ 平面内的正弦曲线上。
// --------------------------------------------------------------------
glm::vec3 SineParticlePlugin::positionAt(float time) const {
    const float x = origin_.x +
                    pathLength_ * 0.5f *
                        std::sin(speed_ * time + phase_);
    const float z = origin_.z +
                    amplitude_ *
                        std::sin(frequency_ * (x - origin_.x) + phase_);

    return glm::vec3(x, origin_.y, z);
}

void SineParticlePlugin::onAttach(Object& owner) {
    origin_ = owner.position();
    time_ = 0.0f;
    owner.setPosition(positionAt(0.0f));
}

void SineParticlePlugin::update(Object& owner, float deltaSeconds) {
    time_ += deltaSeconds;
    owner.setPosition(positionAt(time_));
}

void SineParticlePlugin::setTime(Object& owner, float time) {
    time_ = time;
    owner.setPosition(positionAt(time_));
}

bool SineParticlePlugin::collectInstances(
    const Object& owner, std::vector<ObjectInstance>& out) const {
    if (owner.mesh().empty()) {
        return false;
    }

    // ---- 主粒子 ----
    const float visualSize = std::max(size_ * 4.0f, 0.02f);
    const glm::mat4 particleTransform =
        glm::translate(glm::mat4(1.0f), owner.position()) *
        glm::scale(glm::mat4(1.0f), glm::vec3(visualSize));

    out.push_back({&owner.mesh(), particleTransform, owner.color(), false});

    // ---- 透明发光轨迹 ----
    if (trailDuration_ <= 0.0f || trailSamples_ < 2 ||
        trailPointSize_ <= 0.0f) {
        return true;
    }

    // 基于粒子颜色提亮; 再通过 alpha 渐隐。
    const glm::vec3 baseColor = glm::vec3(owner.color());
    const glm::vec3 brightColor = glm::min(
        glm::vec3(1.0f), baseColor * 1.9f + glm::vec3(0.12f));

    const int sampleCount = trailSamples_;
    for (int i = 0; i < sampleCount; ++i) {
        // 0 = 最老的轨迹点, 1 = 最新的轨迹点。
        const float fade =
            static_cast<float>(i) / static_cast<float>(sampleCount - 1);

        // 从 time_ - trailDuration 到 time_ 取样。
        const float sampleTime =
            time_ - trailDuration_ * (1.0f - fade);
        const glm::vec3 samplePosition = positionAt(sampleTime);

        // 越新的点越亮、越不透明; 但整体 alpha 仍低于主粒子。
        const float alpha = owner.color().a *
                            (0.10f + 0.55f * fade) *
                            (0.65f + 0.35f * fade);

        const glm::mat4 trailTransform =
            glm::translate(glm::mat4(1.0f), samplePosition) *
            glm::scale(glm::mat4(1.0f),
                       glm::vec3(trailPointSize_));

        out.push_back({&owner.mesh(), trailTransform,
                       glm::vec4(brightColor, alpha), true});
    }

    return true;
}

std::string SineParticlePlugin::name() const {
    return "SineParticlePlugin";
}

std::string SineParticlePlugin::summary() const {
    std::ostringstream out;
    out << "SineParticlePlugin: A=" << amplitude_
        << ", L=" << pathLength_
        << ", f=" << frequency_
        << ", speed=" << speed_
        << ", trail=" << trailDuration_ << "s";
    return out.str();
}

void SineParticlePlugin::save(BinaryWriter& out) const {
    out.writeVec3(origin_);
    out.writeFloat(amplitude_);
    out.writeFloat(pathLength_);
    out.writeFloat(frequency_);
    out.writeFloat(speed_);
    out.writeFloat(phase_);
    out.writeFloat(size_);
    out.writeFloat(trailDuration_);
    out.writeU32(static_cast<std::uint32_t>(trailSamples_));
    out.writeFloat(trailPointSize_);
    out.writeFloat(time_);
}

bool SineParticlePlugin::load(BinaryReader& in) {
    origin_ = in.readVec3();
    amplitude_ = std::max(0.0f, in.readFloat());
    pathLength_ = std::max(0.0f, in.readFloat());
    frequency_ = in.readFloat();
    speed_ = in.readFloat();
    phase_ = in.readFloat();
    size_ = std::max(0.0f, in.readFloat());
    trailDuration_ = std::max(0.0f, in.readFloat());
    trailSamples_ = std::clamp(
        static_cast<int>(in.readU32()), 2, 160);
    trailPointSize_ = std::max(0.0f, in.readFloat());
    time_ = in.readFloat();
    return in.ok();
}

float SineParticlePlugin::amplitude() const {
    return amplitude_;
}

void SineParticlePlugin::setAmplitude(float value) {
    amplitude_ = std::max(0.0f, value);
}

float SineParticlePlugin::pathLength() const {
    return pathLength_;
}

void SineParticlePlugin::setPathLength(float value) {
    pathLength_ = std::max(0.01f, value);
}

float SineParticlePlugin::frequency() const {
    return frequency_;
}

void SineParticlePlugin::setFrequency(float value) {
    frequency_ = value;
}

float SineParticlePlugin::speed() const {
    return speed_;
}

void SineParticlePlugin::setSpeed(float value) {
    speed_ = value;
}

float SineParticlePlugin::phase() const {
    return phase_;
}

void SineParticlePlugin::setPhase(float value) {
    phase_ = value;
}

float SineParticlePlugin::size() const {
    return size_;
}

void SineParticlePlugin::setSize(float value) {
    size_ = std::max(0.0f, value);
}

float SineParticlePlugin::trailDuration() const {
    return trailDuration_;
}

void SineParticlePlugin::setTrailDuration(float value) {
    trailDuration_ = std::max(0.0f, value);
}

int SineParticlePlugin::trailSamples() const {
    return trailSamples_;
}

void SineParticlePlugin::setTrailSamples(int value) {
    trailSamples_ = std::clamp(value, 2, 160);
}

float SineParticlePlugin::trailPointSize() const {
    return trailPointSize_;
}

void SineParticlePlugin::setTrailPointSize(float value) {
    trailPointSize_ = std::max(0.001f, value);
}
