// ====================================================================
// sine_motion_plugin.cpp
// ====================================================================

#include "sine_motion_plugin.h"

#include "byte_stream.h"
#include "object.h"

#include <algorithm>
#include <cmath>
#include <sstream>

SineMotionPlugin::SineMotionPlugin(float horizontalAmplitude,
                                   float verticalAmplitude,
                                   float frequency,
                                   float speed,
                                   float phase)
    : horizontalAmplitude_(std::max(0.0f, horizontalAmplitude)),
      verticalAmplitude_(std::max(0.0f, verticalAmplitude)),
      frequency_(frequency),
      speed_(speed),
      phase_(phase) {
}

glm::vec3 SineMotionPlugin::positionAt(float time) const {
    const float x = origin_.x +
                    horizontalAmplitude_ *
                        std::sin(speed_ * time + phase_);
    const float z = origin_.z +
                    verticalAmplitude_ *
                        std::sin(frequency_ * (x - origin_.x) + phase_);
    return glm::vec3(x, origin_.y, z);
}

void SineMotionPlugin::onAttach(Object& owner) {
    origin_ = owner.position();
    time_ = 0.0f;
    owner.setPosition(positionAt(0.0f));
}

void SineMotionPlugin::update(Object& owner, float deltaSeconds) {
    time_ += deltaSeconds;
    owner.setPosition(positionAt(time_));
}

void SineMotionPlugin::setTime(Object& owner, float time) {
    time_ = time;
    owner.setPosition(positionAt(time_));
}

std::string SineMotionPlugin::name() const {
    return "SineMotionPlugin";
}

std::string SineMotionPlugin::summary() const {
    std::ostringstream out;
    out << "SineMotionPlugin: X=" << horizontalAmplitude_
        << ", Z=" << verticalAmplitude_
        << ", f=" << frequency_
        << ", speed=" << speed_;
    return out.str();
}

void SineMotionPlugin::save(BinaryWriter& out) const {
    out.writeVec3(origin_);
    out.writeFloat(horizontalAmplitude_);
    out.writeFloat(verticalAmplitude_);
    out.writeFloat(frequency_);
    out.writeFloat(speed_);
    out.writeFloat(phase_);
    out.writeFloat(time_);
}

bool SineMotionPlugin::load(BinaryReader& in) {
    origin_ = in.readVec3();
    horizontalAmplitude_ = std::max(0.0f, in.readFloat());
    verticalAmplitude_ = std::max(0.0f, in.readFloat());
    frequency_ = in.readFloat();
    speed_ = in.readFloat();
    phase_ = in.readFloat();
    time_ = in.readFloat();
    return in.ok();
}

float SineMotionPlugin::horizontalAmplitude() const {
    return horizontalAmplitude_;
}

void SineMotionPlugin::setHorizontalAmplitude(float value) {
    horizontalAmplitude_ = std::max(0.0f, value);
}

float SineMotionPlugin::verticalAmplitude() const {
    return verticalAmplitude_;
}

void SineMotionPlugin::setVerticalAmplitude(float value) {
    verticalAmplitude_ = std::max(0.0f, value);
}

float SineMotionPlugin::frequency() const {
    return frequency_;
}

void SineMotionPlugin::setFrequency(float value) {
    frequency_ = value;
}

float SineMotionPlugin::speed() const {
    return speed_;
}

void SineMotionPlugin::setSpeed(float value) {
    speed_ = value;
}

float SineMotionPlugin::phase() const {
    return phase_;
}

void SineMotionPlugin::setPhase(float value) {
    phase_ = value;
}
