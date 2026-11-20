// ====================================================================
// coordinate_system_plugin.h —— 坐标系行为插件
//
// 一个坐标系现在由两部分组成:
//
//   Object                  : 网格 + 原点坐标 + 颜色 + 插件列表
//   CoordinateSystemPlugin  : 轴长 + 是否显示网格
//
// 插件通过 collectInstances() 用同一个 Mesh 生成六个实例:
//   X 轴: 红色, 沿 +X / -X 方向拉伸
//   Y 轴: 绿色, 沿 +Y / -Y 方向拉伸
//   Z 轴: 蓝色, 沿 +Z / -Z 方向拉伸
//
// Object 本身完全不知道"坐标系"这个概念。
// ====================================================================
#pragma once

#include "plugin.h"

#include <string>

class CoordinateSystemPlugin : public Plugin {
public:
    /**
     * 构造坐标系插件
     * @param axisLength 坐标轴长度, 最小 0.01
     * @param showGrid   是否显示网格 (当前保存参数, 供以后接入网格渲染)
     */
    explicit CoordinateSystemPlugin(float axisLength = 1.0f,
                                    bool showGrid = true);

    /**
     * 生成三根坐标轴实例
     *
     * 每个实例都使用 owner.mesh() 作为基础网格,
     * 通过不同的平移/缩放变换把它拉成细长条。
     *
     * @return true 表示已经生成实例
     */
    bool collectInstances(const Object& owner,
                          std::vector<ObjectInstance>& out) const override;

    // 插件名, 固定返回 "CoordinateSystemPlugin"
    std::string name() const override;

    // 一行摘要
    std::string summary() const override;

    void save(BinaryWriter& out) const override;
    bool load(BinaryReader& in) override;

    // ---- 参数读写 (菜单使用) ----

    float axisLength() const;
    void setAxisLength(float axisLength);

    bool showGrid() const;
    void setShowGrid(bool showGrid);

private:
    // 坐标轴长度
    float axisLength_ = 1.0f;

    // 是否显示网格; 目前只保存状态
    bool showGrid_ = true;
};
