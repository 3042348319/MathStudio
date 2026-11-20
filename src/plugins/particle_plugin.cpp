// ====================================================================
// particle_plugin.cpp —— 粒子行为插件的实现
// ====================================================================

#include "particle_plugin.h"

#include "byte_stream.h"
#include "object.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <sstream>

// --------------------------------------------------------------------
// 构造函数
//
// lifetime 至少 0.01 秒:
//   * 避免 age >= lifetime 在每一帧都立即成立;
//   * 也避免以后用 lifetime 做除法时出现 0。
// size 不能为负。
// --------------------------------------------------------------------
ParticlePlugin::ParticlePlugin(glm::vec3 velocity,
                               float lifetime,
                               float size)
    : velocity_(velocity),
      lifetime_(std::max(0.01f, lifetime)),
      size_(std::max(0.0f, size)) {
}

// --------------------------------------------------------------------
// 挂载钩子
//
// Object::addPlugin<ParticlePlugin>() 构造完插件后调用。
// 此时 Object 的坐标已经是初始位置, 直接记录即可。
// --------------------------------------------------------------------
void ParticlePlugin::onAttach(Object& owner) {
    origin_ = owner.position();
    age_ = 0.0f;
}

// --------------------------------------------------------------------
// 每帧更新
//
// 注意: 这里修改的是 Object 的 position, 不是插件自己的坐标。
// 所以渲染器只需要读取 Object::position(), 不需要知道粒子的存在。
// --------------------------------------------------------------------
void ParticlePlugin::update(Object& owner, float deltaSeconds) {
    // 先累加年龄。
    age_ += deltaSeconds;

    if (age_ >= lifetime_) {
        // ---- 生命周期结束 ----
        // 年龄归零, 并回到出生点, 形成循环发射。
        age_ = 0.0f;
        owner.setPosition(origin_);
    } else {
        // ---- 正常运动 ----
        // 匀速直线运动: position += velocity * dt。
        owner.setPosition(owner.position() + velocity_ * deltaSeconds);
    }
}

// --------------------------------------------------------------------
// 时间轴定位
//
// 粒子运动是确定性的:
//   位置 = 出生点 + 速度 * (time mod lifetime)
// 因此拖动时间轴可以精确地向前/向后跳转。
// --------------------------------------------------------------------
void ParticlePlugin::setTime(Object& owner, float time) {
    float localTime = std::fmod(time, lifetime_);
    if (localTime < 0.0f) {
        localTime += lifetime_;
    }

    age_ = localTime;
    owner.setPosition(origin_ + velocity_ * localTime);
}

// --------------------------------------------------------------------
// 生成绘制实例
//
// 如果网格为空, 返回 false, 让 Object 决定是否使用默认实例。
// 正常情况下返回 true, 表示"这个对象由我负责画"。
// --------------------------------------------------------------------
bool ParticlePlugin::collectInstances(
    const Object& owner,
    std::vector<ObjectInstance>& out) const {
    // 没有网格就没有可画的东西。
    if (owner.mesh().empty()) {
        return false;
    }

    // 视觉尺寸至少 0.02, 避免缩放到 0 后完全看不见。
    // 乘 4 是为了让 size 参数更接近"粒子直径"的直观感觉。
    const float visualSize = std::max(size_ * 4.0f, 0.02f);

    // 变换顺序 (从右往左作用于顶点):
    //   1. 先按 visualSize 缩放局部网格;
    //   2. 再平移到 Object 的世界坐标。
    const glm::mat4 transform =
        glm::translate(glm::mat4(1.0f), owner.position()) *
        glm::scale(glm::mat4(1.0f), glm::vec3(visualSize));

    // 网格指针来自 Object, 生命周期由 Object 保证。
    // 颜色也直接使用 Object 的颜色。
    out.push_back({&owner.mesh(), transform, owner.color()});

    // 告诉 Object: 我已经生成了实例, 不要再画默认本体。
    return true;
}

// --------------------------------------------------------------------
// 插件名
// --------------------------------------------------------------------
std::string ParticlePlugin::name() const {
    return "ParticlePlugin";
}

// --------------------------------------------------------------------
// 摘要
// --------------------------------------------------------------------
std::string ParticlePlugin::summary() const {
    std::ostringstream out;

    out << "ParticlePlugin: v=("
        << velocity_.x << ", "
        << velocity_.y << ", "
        << velocity_.z << ")"
        << ", lifetime=" << lifetime_
        << ", size=" << size_;

    return out.str();
}

void ParticlePlugin::save(BinaryWriter& out) const {
    out.writeVec3(origin_);
    out.writeVec3(velocity_);
    out.writeFloat(lifetime_);
    out.writeFloat(size_);
    out.writeFloat(age_);
}

bool ParticlePlugin::load(BinaryReader& in) {
    origin_ = in.readVec3();
    velocity_ = in.readVec3();
    lifetime_ = std::max(0.01f, in.readFloat());
    size_ = std::max(0.0f, in.readFloat());
    age_ = std::max(0.0f, in.readFloat());
    return in.ok();
}

// --------------------------------------------------------------------
// 速度读写
// --------------------------------------------------------------------
const glm::vec3& ParticlePlugin::velocity() const {
    return velocity_;
}

void ParticlePlugin::setVelocity(const glm::vec3& velocity) {
    velocity_ = velocity;
}

// --------------------------------------------------------------------
// 生命周期读写
// --------------------------------------------------------------------
float ParticlePlugin::lifetime() const {
    return lifetime_;
}

void ParticlePlugin::setLifetime(float lifetime) {
    // 和构造函数保持同样的下限。
    lifetime_ = std::max(0.01f, lifetime);
}

// --------------------------------------------------------------------
// 尺寸读写
// --------------------------------------------------------------------
float ParticlePlugin::size() const {
    return size_;
}

void ParticlePlugin::setSize(float size) {
    size_ = std::max(0.0f, size);
}

// --------------------------------------------------------------------
// 当前年龄
// --------------------------------------------------------------------
float ParticlePlugin::age() const {
    return age_;
}
