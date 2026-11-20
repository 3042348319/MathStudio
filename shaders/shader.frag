#version 450

// ====================================================================
// 片元着色器 (Fragment Shader)
// 由 CMake 在构建时用 glslc 编译为 frag.spv
// 职责: 对每个片元(像素)采样纹理, 与顶点颜色相乘后输出最终颜色
// ====================================================================

// 组合图像采样器 (Combined Image Sampler)
// layout(binding = 1) 对应 C++ 端描述符集布局中的 binding 1
// 纹理采样方式由 C++ 端的 VkSampler 决定 (线性过滤, 各向异性, Mipmap 等)
layout(binding = 1) uniform sampler2D texSampler;

// 片元着色器的输入 (与顶点着色器的输出对应, 经过光栅化插值)
layout(location = 0) in vec3 fragColor;    // 插值后的顶点颜色
layout(location = 1) in vec2 fragTexCoord; // 插值后的纹理坐标

// 颜色附件输出
// layout(location = 0) 对应渲染通道中唯一的颜色附件 (attachment 0)
// 该值写入交换链图像, 最终显示到屏幕
layout(location = 0) out vec4 outColor;

void main() {
    // texture() 内置函数: 使用 texSampler 的采样设置对 fragTexCoord 采样
    // 返回的 vec4 为 RGBA 颜色值 (范围 0~1)
    // 与顶点颜色相乘: 让立方体每个面的颜色与棋盘格纹理叠加, 更直观
    outColor = texture(texSampler, fragTexCoord) * vec4(fragColor, 1.0);
}
