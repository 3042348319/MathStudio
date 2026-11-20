// ====================================================================
// object_instance.h —— 一次绘制实例
//
// 这个文件定义渲染器真正需要的最小数据单位。
//
// 为什么需要 ObjectInstance？
//   Object 负责保存"对象是什么" (网格/坐标/颜色/插件);
//   Plugin 负责决定"这个对象要画成什么样" (一个粒子? 三根坐标轴?);
//   ObjectInstance 负责把最终结果交给渲染器。
//
// 这样 SubWindow / ViewportRenderer 只需要认识 ObjectInstance,
// 不需要知道 Object 或 Plugin 的具体类型。
// ====================================================================
#pragma once

#include "mesh.h"

#include <glm/glm.hpp>

/**
 * 一次实例化绘制所需的数据
 *
 * 一个 Object 可能生成多个 ObjectInstance。
 * 例如 CoordinateSystemPlugin 会生成三个实例, 分别表示 X/Y/Z 轴。
 */
struct ObjectInstance {
    // 本次绘制使用哪个网格。
    // 这里保存指针而不是拷贝 Mesh, 避免每帧复制顶点数据。
    // 指针指向 Object 内部长期存活的 Mesh, 生命周期由 Object 保证。
    const Mesh* mesh = nullptr;

    // 模型矩阵: 把 Mesh 的局部坐标变换到世界坐标。
    // 默认单位矩阵表示不缩放、不旋转、不平移。
    glm::mat4 transform{1.0f};

    // 本次绘制的 RGBA 颜色。
    // 顶点着色器会把这个颜色作为 push constant 传给片元着色器。
    glm::vec4 color{1.0f};

    // 是否使用透明管线绘制。
    // 粒子的运动轨迹使用透明混合; 普通物体保持不透明。
    bool transparent = false;
};
