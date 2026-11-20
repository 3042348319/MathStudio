#version 450

// ====================================================================
// 无限网格地板 —— 片元着色器
//
// 对每个像素从相机发一条射线, 与 z = 0 平面求交:
//   * 交点存在时, 用世界坐标计算 1 单位/5 单位网格线;
//   * 距离越远网格越淡, 形成"无限延伸"的视觉效果;
//   * z = 0 平面上的世界 X/Y 轴分别用红/绿色高亮。
// ====================================================================

layout(binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
    mat4 invViewProj;
    vec4 cameraPosition;
    vec4 screenSize;     // xy = 当前视口像素尺寸
    vec4 screenOffset;   // xy = 当前视口在 framebuffer 中的偏移
} camera;

layout(location = 0) out vec4 outColor;

/**
 * 计算网格线强度
 * @param worldXY 世界 XY 坐标
 * @param spacing 网格间距 (世界单位)
 */
float gridLine(vec2 worldXY, float spacing) {
    const vec2 coord = worldXY / spacing;
    const vec2 derivative = fwidth(coord);
    const vec2 grid =
        abs(fract(coord - 0.5) - 0.5) / max(derivative, vec2(1e-6));
    return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main() {
    // 屏幕像素 -> NDC (Vulkan 的 y 轴向下)
    // gl_FragCoord 是 framebuffer 坐标, 停靠浮窗时 3D 视口有偏移,
    // 因此先减去 screenOffset 再归一化。
    const vec2 uv = (gl_FragCoord.xy - camera.screenOffset.xy) /
                    camera.screenSize.xy;
    const vec2 ndc = uv * 2.0 - 1.0;

    // 反投影出近平面/远平面上的世界坐标
    const vec4 nearHomogeneous =
        camera.invViewProj * vec4(ndc, 0.0, 1.0);
    const vec4 farHomogeneous =
        camera.invViewProj * vec4(ndc, 1.0, 1.0);
    const vec3 nearPoint = nearHomogeneous.xyz / nearHomogeneous.w;
    const vec3 farPoint = farHomogeneous.xyz / farHomogeneous.w;
    const vec3 ray = farPoint - nearPoint;

    const vec3 background = vec3(0.03, 0.035, 0.06);

    // 视线严格平行于地面时没有交点, 显示背景色。
    // 注意: 相机可以旋转到地面下方, 因此这里不能只接受 ray.z < 0;
    // 只要射线与 z = 0 平面有正距离交点, 从上方/下方都应该显示网格。
    if (abs(ray.z) < 1e-6) {
        outColor = vec4(background, 1.0);
        return;
    }

    const float t = -nearPoint.z / ray.z;
    if (t <= 0.0) {
        outColor = vec4(background, 1.0);
        return;
    }

    const vec3 world = nearPoint + ray * t;
    const float distanceToCamera =
        length(world.xy - camera.cameraPosition.xy);
    const float fade =
        1.0 - smoothstep(35.0, 110.0, distanceToCamera);

    const float minor = gridLine(world.xy, 1.0) * 0.35;
    const float major = gridLine(world.xy, 5.0) * 0.65;

    // 世界 X 轴 (y = 0) 红色, 世界 Y 轴 (x = 0) 绿色
    const float xAxis = 1.0 - smoothstep(0.0, 0.06, abs(world.y));
    const float yAxis = 1.0 - smoothstep(0.0, 0.06, abs(world.x));

    vec3 color = background;
    color = mix(color, vec3(0.28, 0.31, 0.38), minor * fade);
    color = mix(color, vec3(0.52, 0.56, 0.64), major * fade);
    color = mix(color, vec3(0.85, 0.22, 0.22), xAxis * fade);
    color = mix(color, vec3(0.22, 0.85, 0.32), yAxis * fade);

    outColor = vec4(color, 1.0);
}
