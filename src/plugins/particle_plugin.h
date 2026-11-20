// ====================================================================
// particle_plugin.h —— 粒子行为插件
//
// 一个粒子现在由两部分组成:
//
//   Object         : 网格 + 当前坐标 + 颜色 + 插件列表
//   ParticlePlugin : 速度 + 生命周期 + 尺寸 + 年龄
//
// ParticlePlugin 做三件事:
//   onAttach        : 记录粒子出生位置;
//   update          : 每帧移动 Object, 生命周期结束时回到出生点;
//   collectInstances: 生成一个按 size 缩放的网格实例。
//
// 这样 Object 本身完全不知道"粒子"这个概念。
// ====================================================================
#pragma once

#include "plugin.h"

#include <glm/glm.hpp>

#include <string>

class ParticlePlugin : public Plugin {
public:
    /**
     * 构造粒子插件
     *
     * @param velocity 速度 (单位/秒), 会累加到 Object 的坐标
     * @param lifetime 生命周期 (秒)
     * @param size     粒子视觉尺寸, 实际绘制时还会再乘 4
     */
    explicit ParticlePlugin(
        glm::vec3 velocity = glm::vec3(0.0f, 0.5f, 0.0f),
        float lifetime = 3.0f,
        float size = 0.1f);

    /**
     * 挂载时记录初始位置
     * Object::addPlugin() 会调用它。
     */
    void onAttach(Object& owner) override;

    /**
     * 每帧更新
     *
     * 执行步骤:
     *   1. age_ += deltaSeconds;
     *   2. 如果 age_ >= lifetime_:
     *        - age_ 归零;
     *        - 把 Object 放回 origin_;
     *   3. 否则:
     *        - Object.position += velocity_ * deltaSeconds。
     */
    void update(Object& owner, float deltaSeconds) override;

    /**
     * 时间轴定位
     * 根据绝对时间计算粒子在匀速直线运动中的位置。
     */
    void setTime(Object& owner, float time) override;

    /**
     * 生成粒子绘制实例
     *
     * 变换 = 平移到 Object.position * 按 size 缩放。
     * @return true 表示已经生成实例
     */
    bool collectInstances(const Object& owner,
                          std::vector<ObjectInstance>& out) const override;

    // 插件类型名, 固定返回 "ParticlePlugin"
    std::string name() const override;

    // 一行摘要, 用于对象列表/调试
    std::string summary() const override;

    void save(BinaryWriter& out) const override;
    bool load(BinaryReader& in) override;

    // ---- 参数读写 (菜单使用) ----

    const glm::vec3& velocity() const;
    void setVelocity(const glm::vec3& velocity);

    float lifetime() const;
    void setLifetime(float lifetime);

    float size() const;
    void setSize(float size);

    float age() const;

private:
    // onAttach 时记录的出生位置; 生命周期结束后回到这里
    glm::vec3 origin_{0.0f};

    // 每秒钟移动的距离
    glm::vec3 velocity_{0.0f, 0.5f, 0.0f};

    // 生命周期, 单位秒; 最小 0.01, 避免除零/立刻无限重置
    float lifetime_ = 3.0f;

    // 视觉尺寸; 实际绘制时乘 4
    float size_ = 0.1f;

    // 当前年龄, 单位秒
    float age_ = 0.0f;
};
