#version 450

// ====================================================================
// 子窗口对象顶点着色器
// 由 CMake 在构建时用 glslc 编译为 object_vert.spv
//
// 与主窗口的 shader.vert 区别:
//   * 相机 view/proj 放在统一缓冲 (binding = 0);
//   * 每个对象的 model 矩阵和颜色通过 push constant 传入,
//     因此同一个管线可以绘制任意数量的对象实例。
// ====================================================================

layout(binding = 0) uniform Camera {
    mat4 view;  // 视图矩阵
    mat4 proj;  // 投影矩阵
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;  // 当前对象的模型矩阵
    vec4 color;  // 当前对象的颜色
} pc;

layout(location = 0) in vec3 inPosition;  // 顶点位置
layout(location = 1) in vec3 inColor;     // 顶点颜色 (保留以匹配共享顶点缓冲)

layout(location = 0) out vec3 fragColor;  // 对象颜色
layout(location = 1) out vec3 vWorldPos;  // 世界坐标 (片元着色器用于简单光照)
layout(location = 2) out float fragAlpha; // 透明度 (轨迹使用)

void main() {
    const vec4 worldPosition = pc.model * vec4(inPosition, 1.0);
    vWorldPos = worldPosition.xyz;
    gl_Position = camera.proj * camera.view * worldPosition;

    // 对象颜色直接来自 push constant; 不使用主窗口的棋盘格纹理。
    fragColor = pc.color.rgb;
    fragAlpha = pc.color.a;
}
