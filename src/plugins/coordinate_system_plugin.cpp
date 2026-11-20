// ====================================================================
// coordinate_system_plugin.cpp —— 坐标系插件的实现
// ====================================================================

#include "coordinate_system_plugin.h"

#include "byte_stream.h"
#include "object.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <sstream>

// --------------------------------------------------------------------
// 构造函数
//
// axisLength 至少 0.01, 避免缩放到 0 后三根轴完全看不见。
// --------------------------------------------------------------------
CoordinateSystemPlugin::CoordinateSystemPlugin(float axisLength, bool showGrid)
    : axisLength_(std::max(0.01f, axisLength)),
      showGrid_(showGrid) {
}

// --------------------------------------------------------------------
// 轴端圆锥头网格: 使用 resources/models/cone.mesh。
// 静态加载一次, ObjectInstance 只保存指向它的指针。
// --------------------------------------------------------------------
namespace {

const Mesh& axisHeadMesh() {
    static const Mesh mesh = [] {
        try {
            return Mesh::loadFromResource("cone.mesh");
        } catch (...) {
            return Mesh{};
        }
    }();
    return mesh;
}

/** 轴身使用圆柱网格; 找不到时退回 Object 自身网格。 */
const Mesh& axisShaftMesh() {
    static const Mesh mesh = [] {
        try {
            return Mesh::loadFromResource("cylinder.mesh");
        } catch (...) {
            return Mesh{};
        }
    }();
    return mesh;
}

}  // namespace

// --------------------------------------------------------------------
// 生成坐标轴 + 轴端圆锥头
//
// 以 Object 的 position 为原点:
//   * 每根轴从 -axisLength 延伸到 +axisLength;
//   * 轴身使用 cylinder.mesh, 从原点延伸到圆锥底面;
//   * 圆锥底面正好贴在轴尾, 尖端继续延伸到轴端;
//   * 六个轴端各放一个 cone.mesh 圆锥头。
// --------------------------------------------------------------------
bool CoordinateSystemPlugin::collectInstances(
    const Object& owner,
    std::vector<ObjectInstance>& out) const {
    // 没有网格就无法生成坐标轴。
    if (owner.mesh().empty()) {
        return false;
    }

    // 圆柱轴身直径; 比原来的方形轴更细。
    constexpr float shaftDiameter = 0.040f;

    const float headLength =
        std::clamp(axisLength_ * 0.15f, 0.12f, 0.35f);
    const float headDiameter = shaftDiameter * 2.6f;

    // 圆锥底面所在位置, 也是圆柱轴身的终点。
    const float shaftLength =
        std::max(0.01f, axisLength_ - headLength);

    // 先构造"把局部坐标平移到 Object 原点"的矩阵。
    const glm::mat4 origin =
        glm::translate(glm::mat4(1.0f), owner.position());

    // 轴身: 圆柱沿 Z 轴生成, 通过旋转对齐到 X/Y 轴。
    const auto shaft = [&](const glm::vec3& offset,
                           const glm::mat4& rotation) {
        return origin *
               glm::translate(glm::mat4(1.0f), offset) *
               rotation *
               glm::scale(glm::mat4(1.0f),
                          glm::vec3(shaftDiameter, shaftDiameter,
                                    shaftLength));
    };

    // 轴端圆锥头: 平移 + 旋转 + 缩放。
    const auto head = [&](const glm::vec3& offset,
                          const glm::mat4& rotation) {
        return origin *
               glm::translate(glm::mat4(1.0f), offset) *
               rotation *
               glm::scale(glm::mat4(1.0f),
                          glm::vec3(headDiameter, headDiameter,
                                    headLength));
    };

    constexpr float halfPi = 1.57079632679f;
    const float shaftCenter = shaftLength * 0.5f;
    const float headCenter = axisLength_ - headLength * 0.5f;
    const glm::mat4 identity(1.0f);

    const glm::mat4 rotateXPositive =
        glm::rotate(identity, -halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 rotateXNegative =
        glm::rotate(identity, halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 rotateYPositive =
        glm::rotate(identity, halfPi, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 rotateYNegative =
        glm::rotate(identity, -halfPi, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 rotateZNegative =
        glm::rotate(identity, 3.14159265359f,
                    glm::vec3(1.0f, 0.0f, 0.0f));

    const bool hasHeads = !axisHeadMesh().empty();
    const Mesh& shaftMesh =
        axisShaftMesh().empty() ? owner.mesh() : axisShaftMesh();

    // ---- X 轴 (红色, 正负两侧) ----
    out.push_back({
        &shaftMesh,
        shaft(glm::vec3(shaftCenter, 0.0f, 0.0f), rotateYPositive),
        glm::vec4(0.95f, 0.25f, 0.25f, 1.0f)
    });
    out.push_back({
        &shaftMesh,
        shaft(glm::vec3(-shaftCenter, 0.0f, 0.0f), rotateYNegative),
        glm::vec4(0.62f, 0.16f, 0.16f, 1.0f)
    });
    if (hasHeads) {
        out.push_back({
            &axisHeadMesh(),
            head(glm::vec3(headCenter, 0.0f, 0.0f), rotateYPositive),
            glm::vec4(0.95f, 0.25f, 0.25f, 1.0f)
        });
        out.push_back({
            &axisHeadMesh(),
            head(glm::vec3(-headCenter, 0.0f, 0.0f), rotateYNegative),
            glm::vec4(0.62f, 0.16f, 0.16f, 1.0f)
        });
    }

    // ---- Y 轴 (绿色, 正负两侧) ----
    out.push_back({
        &shaftMesh,
        shaft(glm::vec3(0.0f, shaftCenter, 0.0f), rotateXPositive),
        glm::vec4(0.25f, 0.95f, 0.35f, 1.0f)
    });
    out.push_back({
        &shaftMesh,
        shaft(glm::vec3(0.0f, -shaftCenter, 0.0f), rotateXNegative),
        glm::vec4(0.16f, 0.62f, 0.22f, 1.0f)
    });
    if (hasHeads) {
        out.push_back({
            &axisHeadMesh(),
            head(glm::vec3(0.0f, headCenter, 0.0f), rotateXPositive),
            glm::vec4(0.25f, 0.95f, 0.35f, 1.0f)
        });
        out.push_back({
            &axisHeadMesh(),
            head(glm::vec3(0.0f, -headCenter, 0.0f), rotateXNegative),
            glm::vec4(0.16f, 0.62f, 0.22f, 1.0f)
        });
    }

    // ---- Z 轴 (蓝色, 正负两侧) ----
    out.push_back({
        &shaftMesh,
        shaft(glm::vec3(0.0f, 0.0f, shaftCenter), identity),
        glm::vec4(0.30f, 0.45f, 1.0f, 1.0f)
    });
    out.push_back({
        &shaftMesh,
        shaft(glm::vec3(0.0f, 0.0f, -shaftCenter), rotateZNegative),
        glm::vec4(0.18f, 0.28f, 0.65f, 1.0f)
    });
    if (hasHeads) {
        out.push_back({
            &axisHeadMesh(),
            head(glm::vec3(0.0f, 0.0f, headCenter), identity),
            glm::vec4(0.30f, 0.45f, 1.0f, 1.0f)
        });
        out.push_back({
            &axisHeadMesh(),
            head(glm::vec3(0.0f, 0.0f, -headCenter), rotateZNegative),
            glm::vec4(0.18f, 0.28f, 0.65f, 1.0f)
        });
    }

    // 已生成实例, Object 不再添加默认本体。
    return true;
}

// --------------------------------------------------------------------
// 插件名
// --------------------------------------------------------------------
std::string CoordinateSystemPlugin::name() const {
    return "CoordinateSystemPlugin";
}

// --------------------------------------------------------------------
// 摘要
// --------------------------------------------------------------------
std::string CoordinateSystemPlugin::summary() const {

    std::ostringstream out;

    out << "CoordinateSystemPlugin: axisLength=" << axisLength_
        << ", showGrid=" << (showGrid_ ? "true" : "false");

    return out.str();
}

void CoordinateSystemPlugin::save(BinaryWriter& out) const {
    out.writeFloat(axisLength_);
    out.writeBool(showGrid_);
}

bool CoordinateSystemPlugin::load(BinaryReader& in) {
    axisLength_ = std::max(0.01f, in.readFloat());
    showGrid_ = in.readBool();
    return in.ok();
}

// --------------------------------------------------------------------
// 轴长读写
// --------------------------------------------------------------------
float CoordinateSystemPlugin::axisLength() const {
    return axisLength_;
}

void CoordinateSystemPlugin::setAxisLength(float axisLength) {
    axisLength_ = std::max(0.01f, axisLength);
}

// --------------------------------------------------------------------
// 网格开关读写
// --------------------------------------------------------------------
bool CoordinateSystemPlugin::showGrid() const {
    return showGrid_;
}

void CoordinateSystemPlugin::setShowGrid(bool showGrid) {
    showGrid_ = showGrid;
}
