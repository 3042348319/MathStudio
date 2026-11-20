// ====================================================================
// object.h —— 场景对象
//
// Object 是场景里最基础的数据单元, 只保存四类数据:
//
//   1. mesh     : 网格 (从 resources/models 加载)
//   2. position : 世界坐标
//   3. color    : RGBA 颜色
//   4. plugins  : 插件列表
//
// 注意: Object 不再有 Particle / CoordinateSystem 这类子类。
// 所有行为 (运动、生命周期、坐标轴、... ) 都由 Plugin 提供:
//
//   Object + ParticlePlugin          -> 一个会移动/重生的粒子
//   Object + CoordinateSystemPlugin  -> 一个三轴坐标系
//   Object + 自定义 Plugin           -> 任意新行为
//
// 这样 Object 可以保持不变, 通过组合插件扩展功能。
// ====================================================================
#pragma once

#include "mesh.h"
#include "object_id.h"
#include "object_instance.h"
#include "plugin.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * 场景对象
 *
 * 只负责保存数据, 不直接调用 Vulkan, 也不关心自己属于哪个窗口。
 * 渲染器通过 collectInstances() 取得"要画哪些网格"。
 */
class Object {
public:
    /**
     * 构造对象
     * @param mesh     网格资源 (会被移动进对象)
     * @param position 初始世界坐标
     * @param color    RGBA 颜色
     * @param name     对象名称; 为空时使用网格名
     */
    Object(Mesh mesh,
           glm::vec3 position = glm::vec3(0.0f),
           glm::vec4 color = glm::vec4(1.0f),
           std::string name = std::string());

    // 虚析构: 为将来可能出现的 Object 派生类型保留安全删除能力。
    virtual ~Object() = default;

    // Object 持有 unique_ptr 插件列表, 并且插件可能保存指向 owner 的引用,
    // 因此禁止拷贝/移动, 避免出现两个对象共享插件或悬空引用。
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    // 对象稳定 ID; 组别/资源管理器用它引用对象
    ObjectId id() const;

    // 对象名称 (可在浮窗或资源管理器中修改)
    const std::string& name() const;
    void setName(std::string name);

    // -----------------------------------------------------------------
    // 网格
    // -----------------------------------------------------------------

    // 只读访问网格; 渲染器通过它取得顶点/索引数据
    const Mesh& mesh() const;

    // 可写访问网格; 允许运行期替换模型 (会与 GPU 缓存指针失配,
    // 目前菜单不直接修改它)
    Mesh& mesh();

    // 替换整个网格
    void setMesh(Mesh mesh);

    // -----------------------------------------------------------------
    // 坐标
    // -----------------------------------------------------------------

    // 只读访问世界坐标
    const glm::vec3& position() const;

    // 修改世界坐标; ParticlePlugin 每帧通过它移动对象
    void setPosition(const glm::vec3& position);

    // -----------------------------------------------------------------
    // 颜色
    // -----------------------------------------------------------------

    // 只读访问 RGBA 颜色
    const glm::vec4& color() const;

    // 修改 RGBA 颜色
    void setColor(const glm::vec4& color);

    // 是否显示; 隐藏后不参与绘制, 但仍保留在对象列表/工程中
    bool visible() const;
    void setVisible(bool visible);

    /**
     * 由坐标生成模型矩阵
     * 当前只做平移, 以后可以在这里扩展旋转/缩放。
     */
    glm::mat4 modelMatrix() const;

    // -----------------------------------------------------------------
    // 插件管理
    // -----------------------------------------------------------------

    /**
     * 以模板方式挂载插件
     *
     * 用法:
     *   object.addPlugin<ParticlePlugin>(velocity, lifetime, size);
     *   object.addPlugin<ParameterPlugin>("emissionRate", "120");
     *
     * 执行步骤:
     *   1. 用参数构造一个 T 对象;
     *   2. 把它放进 unique_ptr 插件列表;
     *   3. 调用 plugin->onAttach(*this), 让插件记录初始状态;
     *   4. 返回引用, 方便继续链式配置。
     *
     * @tparam T 插件类型, 必须派生自 Plugin
     * @param args 传给 T 构造函数的参数
     * @return 新插件的引用
     */
    template <typename T, typename... Args>
    T& addPlugin(Args&&... args) {
        // 编译期检查, 防止把非 Plugin 类型放进插件列表。
        static_assert(std::is_base_of_v<Plugin, T>,
                      "T must derive from Plugin");

        // 先构造插件; make_unique 保证异常安全。
        auto plugin = std::make_unique<T>(std::forward<Args>(args)...);

        // 在 move 之前保存裸引用, 因为 unique_ptr 移动后仍指向同一对象。
        T& reference = *plugin;

        // 所有权转移给 Object。
        plugins_.push_back(std::move(plugin));

        // 通知插件"你已经挂载到这个 Object 上了"。
        // 注意: 必须在 push_back 之后调用, 否则插件列表里还没有它。
        reference.onAttach(*this);

        return reference;
    }

    /**
     * 放入一个已经创建好的插件 (工程加载 / 动态创建)
     *
     * 与模板版本相同: 转移所有权后调用 onAttach(),
     * 便于加载器在恢复参数前先让插件完成挂载。
     */
    Plugin& addPlugin(std::unique_ptr<Plugin> plugin);

    /**
     * 按类型查找插件 (非 const)
     * @return 找到返回指针, 否则返回 nullptr
     */
    template <typename T>
    T* getPlugin() {
        for (const auto& plugin : plugins_) {
            // dynamic_cast 在运行期检查真实类型。
            if (auto* typed = dynamic_cast<T*>(plugin.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    /**
     * 按类型查找插件 (const)
     */
    template <typename T>
    const T* getPlugin() const {
        for (const auto& plugin : plugins_) {
            if (const auto* typed = dynamic_cast<const T*>(plugin.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    /**
     * 按插件名删除插件
     * @return true 表示找到并删除; false 表示没有同名插件
     */
    bool removePlugin(const std::string& name);

    // 插件数量
    std::size_t pluginCount() const;

    // 只读插件列表; 菜单遍历它生成参数控件
    const std::vector<std::unique_ptr<Plugin>>& plugins() const;

    // -----------------------------------------------------------------
    // 显示 / 更新 / 绘制
    // -----------------------------------------------------------------

    // 对象类型名; 当前固定为 "Object"
    virtual std::string typeName() const;

    /**
     * 生成多行描述
     * 包含网格、顶点数、索引数、坐标、颜色、插件摘要。
     * 主要用于调试输出/对象列表。
     */
    virtual std::string describe() const;

    /**
     * 每帧更新
     * Object 自己不做运动, 只把 deltaSeconds 转发给所有插件。
     * 例如 ParticlePlugin::update() 会在这里移动 Object。
     */
    virtual void update(float deltaSeconds);

    /**
     * 把对象定位到绝对动画时间
     *
     * 时间轴拖动/暂停时使用。每个插件根据自己的运动模型
     * 计算 time 时刻的位置, 因此可以向前也可以向后跳转。
     */
    virtual void setAnimationTime(float time);

    /**
     * 把对象参数写成 key=value 文本
     *
     * 当前菜单直接使用 ImGui 控件修改参数, 不依赖这份文本;
     * 保留它是为了以后做保存/加载或调试输出。
     */
    virtual void writeParameters(std::ostream& out) const;

    /**
     * 根据 key=value 修改参数
     * @return true 表示 key 被识别且 value 合法
     */
    virtual bool setParameter(const std::string& key, const std::string& value);

    /**
     * 收集绘制实例
     *
     * 执行顺序:
     *   1. 依次询问插件是否生成实例;
     *   2. 只要任意插件生成了实例, 就不再添加默认实例;
     *   3. 如果所有插件都没有生成实例, 则画一个默认网格。
     *
     * 这样:
     *   * ParticlePlugin 生成 1 个粒子实例;
     *   * CoordinateSystemPlugin 生成 3 个坐标轴实例;
     *   * 只有 ParameterPlugin 的对象仍然画一个默认网格。
     *
     * @param out 输出列表, 调用方负责清空
     */
    virtual void collectInstances(std::vector<ObjectInstance>& out) const;

private:
    // 0. 稳定 ID
    ObjectId id_ = 0;

    // 对象名称
    std::string name_;

    // 1. 网格 (模型数据)
    Mesh mesh_;

    // 2. 世界坐标
    glm::vec3 position_{0.0f};

    // 3. RGBA 颜色
    glm::vec4 color_{1.0f};

    // 是否参与绘制
    bool visible_ = true;

    // 4. 插件列表; unique_ptr 保证多态析构正确
    std::vector<std::unique_ptr<Plugin>> plugins_;
};
