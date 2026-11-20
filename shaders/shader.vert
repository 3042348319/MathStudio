#version 450

// ====================================================================
// 顶点着色器 (Vertex Shader)
// 由 CMake 在构建时用 glslc 编译为 vert.spv
// 职责: 把顶点从模型空间变换到裁剪空间, 并把颜色/纹理坐标传给片元着色器
// ====================================================================

// 统一缓冲对象 (Uniform Buffer Object)
// layout(binding = 0) 对应 C++ 端描述符集布局中的 binding 0
// 布局规则 std140, 与 C++ 端的 UniformBufferObject 结构体一一对应
// 注意: 这里 mat4 与 C++ 端 glm::mat4 的内存布局一致
layout(binding = 0) uniform UniformBufferObject {
    mat4 model; // 模型矩阵: 模型空间 -> 世界空间 (每帧旋转动画)
    mat4 view;  // 视图矩阵: 世界空间 -> 摄像机空间 (相当于摆放摄像机)
    mat4 proj;  // 投影矩阵: 摄像机空间 -> 裁剪空间 (透视投影)
} ubo;

// 顶点着色器的输入属性
// layout(location = N) 与 C++ 端 VkVertexInputAttributeDescription 的 location 对应
layout(location = 0) in vec3 inPosition; // 顶点位置
layout(location = 1) in vec3 inColor;    // 顶点颜色
layout(location = 2) in vec2 inTexCoord; // 纹理坐标 (UV, 范围 0~1)

// 顶点着色器的输出 (会被光栅化器插值后传给片元着色器)
layout(location = 0) out vec3 fragColor;    // 插值后的颜色
layout(location = 1) out vec2 fragTexCoord; // 插值后的纹理坐标

// 程序入口 (SPIR-V 中默认入口名为 main, 也可通过 pName 修改)
void main() {
    // 变换顺序: 模型 -> 世界 -> 摄像机 -> 裁剪空间
    // 这是 Vulkan 中最常见的顶点变换链
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);

    // 透传给片元着色器
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
