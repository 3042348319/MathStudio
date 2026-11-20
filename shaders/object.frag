#version 450

// ====================================================================
// 子窗口对象片元着色器
//
// 使用世界坐标的屏幕空间导数计算面法线, 并组合:
//   * 环境光 (所有面都有基础亮度)
//   * 主方向光
//   * 补光
// 让立方体/坐标轴有明暗变化, 看起来更像实体。
// ====================================================================

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    // 由世界坐标在屏幕空间的偏导数得到面法线。
    const vec3 normal = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));

    // 环境光: 即使背光面也保留基础亮度。
    const vec3 ambientLight = vec3(0.30, 0.34, 0.42);

    // 主方向光。
    const vec3 keyLightColor = vec3(1.0, 0.95, 0.85);
    const vec3 keyLightDirection = normalize(vec3(0.4, 0.7, 0.5));
    const float key = abs(dot(normal, keyLightDirection));

    // 补光: 从相反方向补一点冷色, 避免暗面死黑。
    const vec3 fillLightColor = vec3(0.25, 0.30, 0.40);
    const vec3 fillLightDirection = normalize(vec3(-0.6, -0.3, 0.4));
    const float fill = abs(dot(normal, fillLightDirection));

    const vec3 lighting = ambientLight +
                          keyLightColor * key * 0.75 +
                          fillLightColor * fill * 0.35;

    outColor = vec4(min(fragColor * lighting, vec3(1.0)), fragAlpha);
}
