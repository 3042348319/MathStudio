// ====================================================================
// object.cpp —— Object 的实现
//
// 文件结构:
//   1. 匿名命名空间中的辅助函数 (数字解析 / 向量格式化);
//   2. 构造与四个成员的读写;
//   3. 插件管理;
//   4. 更新与实例收集;
//   5. 文本参数的读写。
// ====================================================================

#include "object.h"

#include "parameter_plugin.h"

#include <glm/gtc/matrix_transform.hpp>

#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

// 匿名命名空间: 这些函数只在本 .cpp 内可见, 不会污染全局符号。
namespace {

// 每个 Object 构造时分配一个稳定 ID, 供组别/资源管理器引用。
ObjectId nextObjectId = 1;

/**
 * 把字符串解析成 float
 *
 * 额外检查 consumed == text.size():
 *   * std::stof("1.5abc") 会成功并返回 1.5;
 *   * 但这里希望整个字符串都必须是数字, 所以 "1.5abc" 返回 false。
 *
 * @param text  输入字符串
 * @param value 输出参数; 只有解析成功时才写入
 * @return true 表示完整解析成功
 */
bool parseFloat(const std::string& text, float& value) {
    try {
        std::size_t consumed = 0;

        // std::stof 会跳过前导空白并解析数字;
        // consumed 记录它实际读了多少个字符。
        const float parsed = std::stof(text, &consumed);

        // 必须吃掉整个字符串, 否则视为格式错误。
        if (consumed != text.size()) {
            return false;
        }

        value = parsed;
        return true;
    } catch (...) {
        // 空字符串、非法字符、超出 float 范围等都会抛异常。
        return false;
    }
}

/**
 * 把 vec3 格式化成 "(x, y, z)"
 * 固定两位小数, 便于调试输出对齐。
 */
std::string formatVec3(const glm::vec3& value) {
    std::ostringstream out;

    out << std::fixed << std::setprecision(2)
        << '(' << value.x << ", " << value.y << ", " << value.z << ')';

    return out.str();
}

/**
 * 把 vec4 格式化成 "(x, y, z, w)"
 */
std::string formatVec4(const glm::vec4& value) {
    std::ostringstream out;

    out << std::fixed << std::setprecision(2)
        << '(' << value.x << ", " << value.y << ", "
        << value.z << ", " << value.w << ')';

    return out.str();
}

}  // namespace

// --------------------------------------------------------------------
// 构造函数
//
// mesh 按值传入后 move 到成员, 避免复制顶点数组。
// position / color 是小型 POD, 直接拷贝。
// --------------------------------------------------------------------
Object::Object(Mesh mesh, glm::vec3 position, glm::vec4 color,
               std::string name)
    : id_(nextObjectId++),
      name_(name.empty() ? mesh.name() : std::move(name)),
      mesh_(std::move(mesh)),
      position_(position),
      color_(color) {
}

ObjectId Object::id() const {
    return id_;
}

const std::string& Object::name() const {
    return name_;
}

void Object::setName(std::string name) {
    name_ = std::move(name);
}

Plugin& Object::addPlugin(std::unique_ptr<Plugin> plugin) {
    if (!plugin) {
        throw std::invalid_argument(
            "Object::addPlugin: plugin is null");
    }

    Plugin& reference = *plugin;
    plugins_.push_back(std::move(plugin));
    reference.onAttach(*this);
    return reference;
}

// --------------------------------------------------------------------
// 只读网格访问
// --------------------------------------------------------------------
const Mesh& Object::mesh() const {
    return mesh_;
}

// --------------------------------------------------------------------
// 可写网格访问
// --------------------------------------------------------------------
Mesh& Object::mesh() {
    return mesh_;
}

// --------------------------------------------------------------------
// 替换网格
//
// 注意: ViewportRenderer 的 meshCache 以 Mesh* 为 key。
// 如果在运行期替换网格, 旧指针对应的 GPU 缓存可能仍然存在。
// 当前菜单不调用此函数。
// --------------------------------------------------------------------
void Object::setMesh(Mesh mesh) {
    mesh_ = std::move(mesh);
}

// --------------------------------------------------------------------
// 读取世界坐标
// --------------------------------------------------------------------
const glm::vec3& Object::position() const {
    return position_;
}

// --------------------------------------------------------------------
// 修改世界坐标
//
// ParticlePlugin::update() 每帧调用它来移动对象。
// --------------------------------------------------------------------
void Object::setPosition(const glm::vec3& position) {
    position_ = position;
}

// --------------------------------------------------------------------
// 读取颜色
// --------------------------------------------------------------------
const glm::vec4& Object::color() const {
    return color_;
}

// --------------------------------------------------------------------
// 修改颜色
// --------------------------------------------------------------------
void Object::setColor(const glm::vec4& color) {
    color_ = color;
}

bool Object::visible() const {
    return visible_;
}

void Object::setVisible(bool visible) {
    visible_ = visible;
}

// --------------------------------------------------------------------
// 模型矩阵
//
// glm::translate(单位矩阵, position) 生成:
//   | 1 0 0 px |
//   | 0 1 0 py |
//   | 0 0 1 pz |
//   | 0 0 0 1  |
//
// 以后如果 Object 支持旋转/缩放, 可以在这里组合:
//   translate * rotate * scale
// --------------------------------------------------------------------
glm::mat4 Object::modelMatrix() const {
    return glm::translate(glm::mat4(1.0f), position_);
}

// --------------------------------------------------------------------
// 按名称删除插件
//
// 使用迭代器遍历, 因为 erase 会使当前迭代器失效;
// erase 返回下一个元素的位置, 直接 return 即可。
// --------------------------------------------------------------------
bool Object::removePlugin(const std::string& name) {
    for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
        if ((*it)->name() == name) {
            plugins_.erase(it);
            return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------
// 插件数量
// --------------------------------------------------------------------
std::size_t Object::pluginCount() const {
    return plugins_.size();
}

// --------------------------------------------------------------------
// 只读插件列表
// --------------------------------------------------------------------
const std::vector<std::unique_ptr<Plugin>>& Object::plugins() const {
    return plugins_;
}

// --------------------------------------------------------------------
// 对象类型名
//
// 现在没有派生类, 所以固定返回 "Object"。
// 菜单标题会显示 "Object - 参数"。
// --------------------------------------------------------------------
std::string Object::typeName() const {
    return "Object";
}

// --------------------------------------------------------------------
// 多行描述
//
// 每行用 \r\n 结尾, 方便 Win32 文本框显示 (虽然现在菜单用 ImGui)。
// 插件部分只输出 summary(), 不展开每个字段。
// --------------------------------------------------------------------
std::string Object::describe() const {
    std::ostringstream out;

    // ---- 基本信息 ----
    out << "  id       : " << id_ << "\r\n";
    out << "  name     : " << name_ << "\r\n";
    out << "  mesh     : " << mesh_.name() << "\r\n";
    out << "  vertices : " << mesh_.vertices().size() << "\r\n";
    out << "  indices  : " << mesh_.indices().size() << "\r\n";
    out << "  position : " << formatVec3(position_) << "\r\n";
    out << "  color    : " << formatVec4(color_) << "\r\n";

    // ---- 插件列表 ----
    out << "  plugins  : " << plugins_.size() << "\r\n";
    for (const auto& plugin : plugins_) {
        out << "    - " << plugin->summary() << "\r\n";
    }

    return out.str();
}

// --------------------------------------------------------------------
// 每帧更新
//
// Object 不实现任何具体运动; 它只负责把 update 事件转发给插件。
// 这就是"组合优于继承"的核心:
//   Object + ParticlePlugin 的对象会移动;
//   Object + CoordinateSystemPlugin 的对象保持静止。
// --------------------------------------------------------------------
void Object::update(float deltaSeconds) {
    for (const auto& plugin : plugins_) {
        plugin->update(*this, deltaSeconds);
    }
}

void Object::setAnimationTime(float time) {
    for (const auto& plugin : plugins_) {
        plugin->setTime(*this, time);
    }
}

// --------------------------------------------------------------------
// 收集绘制实例
//
// 步骤:
//   1. 遍历插件, 调用 collectInstances();
//   2. 记录是否有插件生成了实例;
//   3. 如果有, 直接返回, 不再画默认对象;
//   4. 如果没有且网格非空, 画一个 0.35 倍大小的默认网格。
//
// 注意: out 由调用方清空, 这里只追加。
// --------------------------------------------------------------------
void Object::collectInstances(std::vector<ObjectInstance>& out) const {
    if (!visible_) {
        return;
    }

    bool handled = false;

    for (const auto& plugin : plugins_) {
        // 插件返回 true 表示它已经添加了至少一个实例。
        if (plugin->collectInstances(*this, out)) {
            handled = true;
        }
    }

    // 已经有插件负责绘制, 不要再画一个默认本体。
    if (handled) {
        return;
    }

    // 空网格没有东西可画。
    if (mesh_.empty()) {
        return;
    }

    // 默认外观: 平移 + 0.35 倍缩放。
    const glm::mat4 transform =
        modelMatrix() * glm::scale(glm::mat4(1.0f), glm::vec3(0.35f));

    // 把"网格指针 + 变换 + 颜色"交给渲染器。
    out.push_back({&mesh_, transform, color_});
}

// --------------------------------------------------------------------
// 输出 key=value 文本
//
// 网格本身是只读资源, 只输出名字和统计信息;
// 位置/颜色拆成 x/y/z/r/g/b/a 单独输出, 方便逐项修改。
// 最后把所有 ParameterPlugin 写成 plugin.<key>=value。
// --------------------------------------------------------------------
void Object::writeParameters(std::ostream& out) const {
    // 固定三位小数, 保证输出格式稳定。
    out << std::fixed << std::setprecision(3);

    out << "type=" << typeName() << "\r\n";
    out << "id=" << id_ << "\r\n";
    out << "name=" << name_ << "\r\n";
    out << "mesh=" << mesh_.name() << "\r\n";
    out << "mesh.vertices=" << mesh_.vertices().size() << "\r\n";
    out << "mesh.indices=" << mesh_.indices().size() << "\r\n";

    out << "position.x=" << position_.x << "\r\n";
    out << "position.y=" << position_.y << "\r\n";
    out << "position.z=" << position_.z << "\r\n";

    out << "color.r=" << color_.r << "\r\n";
    out << "color.g=" << color_.g << "\r\n";
    out << "color.b=" << color_.b << "\r\n";
    out << "color.a=" << color_.a << "\r\n";

    // 只序列化通用参数插件; 行为插件由自身实现决定是否保存。
    for (const auto& plugin : plugins_) {
        const auto* parameter =
            dynamic_cast<const ParameterPlugin*>(plugin.get());

        if (parameter != nullptr) {
            out << "plugin." << parameter->key() << "="
                << parameter->value() << "\r\n";
        }
    }
}

// --------------------------------------------------------------------
// 根据 key=value 修改参数
//
// 支持:
//   position.x / position.y / position.z
//   color.r / color.g / color.b / color.a
//   plugin.<key>
//
// 网格字段只读, 返回 false。
// --------------------------------------------------------------------
bool Object::setParameter(const std::string& key, const std::string& value) {
    // 网格是资源, 不允许通过文本参数替换。
    if (key == "mesh" || key == "mesh.vertices" || key == "mesh.indices") {
        return false;
    }

    float parsed = 0.0f;

    // ---- 坐标 ----
    if (key == "position.x" && parseFloat(value, parsed)) {
        position_.x = parsed;
        return true;
    }
    if (key == "position.y" && parseFloat(value, parsed)) {
        position_.y = parsed;
        return true;
    }
    if (key == "position.z" && parseFloat(value, parsed)) {
        position_.z = parsed;
        return true;
    }

    // ---- 颜色 ----
    if (key == "color.r" && parseFloat(value, parsed)) {
        color_.r = parsed;
        return true;
    }
    if (key == "color.g" && parseFloat(value, parsed)) {
        color_.g = parsed;
        return true;
    }
    if (key == "color.b" && parseFloat(value, parsed)) {
        color_.b = parsed;
        return true;
    }
    if (key == "color.a" && parseFloat(value, parsed)) {
        color_.a = parsed;
        return true;
    }

    // ---- 通用参数插件 ----
    constexpr std::string_view pluginPrefix = "plugin.";

    // rfind(prefix, 0) == 0 等价于 starts_with, 但 C++17 没有 starts_with。
    if (key.rfind(pluginPrefix, 0) == 0) {
        // 去掉 "plugin." 前缀, 得到插件自己的 key。
        const std::string pluginKey = key.substr(pluginPrefix.size());

        for (auto& plugin : plugins_) {
            auto* parameter = dynamic_cast<ParameterPlugin*>(plugin.get());

            if (parameter != nullptr && parameter->key() == pluginKey) {
                parameter->setValue(value);
                return true;
            }
        }
    }

    // key 不认识。
    return false;
}
