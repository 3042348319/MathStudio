// ====================================================================
// plugin.h —— 对象插件接口
//
// 设计目标
// --------------------------------------------------------------------
// Object 本身只保存: 网格、坐标、颜色、插件列表。
// "粒子会移动"、"坐标系要画三根轴" 这类行为不写进 Object,
// 而是由 Plugin 通过三个钩子完成:
//
//   onAttach        : 插件刚挂到 Object 上时调用, 用来记录初始状态;
//   update          : 每帧调用, 用来修改 Object 的状态;
//   collectInstances: 渲染前调用, 用来生成一个或多个绘制实例。
//
// 这样新增一种对象行为时, 只需要新增一个 Plugin 子类,
// 不需要修改 Object、SubWindow 或 ViewportRenderer。
// ====================================================================
#pragma once

#include "object_instance.h"

#include <cstddef>
#include <string>
#include <vector>

// Plugin 的钩子需要访问 Object, 但 plugin.h 不能 include object.h,
// 否则 object.h -> plugin.h -> object.h 会形成循环依赖。
// 因此这里只做前向声明, 具体实现放在 plugin 的 .cpp 中。
class Object;
class BinaryWriter;
class BinaryReader;

/**
 * 插件基类
 *
 * 所有插件都必须实现:
 *   * name()    : 插件类型名, 用于查找/删除/显示;
 *   * summary() : 一行摘要, 用于对象列表和调试输出。
 *
 * 其余钩子都有默认空实现, 插件只覆盖自己关心的部分即可。
 */
class Plugin {
public:
    // 通过基类指针删除派生插件时, 必须保证析构函数是虚函数,
    // 否则派生类的成员不会被正确析构。
    virtual ~Plugin() = default;

    /**
     * 插件名称
     * 建议直接返回固定的类名字符串, 例如 "ParticlePlugin"。
     * Object::removePlugin() 会按这个名称删除插件。
     */
    virtual std::string name() const = 0;

    /**
     * 工程文件中的插件类型名
     * 默认与 name() 相同; ParameterPlugin 这类带实例后缀的插件
     * 会覆盖它, 只保存稳定的类型名。
     */
    virtual std::string typeId() const {
        return name();
    }

    /**
     * 是否为动态 DLL 插件。
     *
     * 插件列表用它区分图标:
     *   false -> 静态 C++ 插件, 蓝色方块;
     *   true  -> 动态 DLL 插件, 橙色圆点。
     */
    virtual bool isDynamic() const {
        return false;
    }

    /**
     * 可选: 通用浮点参数接口。
     *
     * 动态插件通过 C ABI 暴露参数; 参数面板不依赖具体插件类型。
     * 静态插件也可以覆盖这些方法, 让参数自动出现在面板中。
     */
    virtual std::size_t parameterCount() const {
        return 0;
    }

    virtual const char* parameterName(std::size_t) const {
        return nullptr;
    }

    virtual float parameterValue(std::size_t) const {
        return 0.0f;
    }

    virtual void setParameterValue(std::size_t, float) {
    }

    /**
     * 插件参数摘要
     * 返回一行人类可读的文本, 例如:
     *   "ParticlePlugin: v=(0, 0.5, 0), lifetime=3, size=0.1"
     */
    virtual std::string summary() const = 0;

    /**
     * 把插件参数写入 .math 工程文件
     * 派生插件只保存自己的状态; 类型名由 name() 提供。
     */
    virtual void save(BinaryWriter&) const {
    }

    /**
     * 从 .math 工程文件恢复参数
     * @return false 表示数据损坏
     */
    virtual bool load(BinaryReader&) {
        return true;
    }

    /**
     * 挂载钩子
     * Object::addPlugin<T>() 在把插件加入列表后会立即调用它。
     *
     * 典型用途:
     *   ParticlePlugin 在这里记录 Object 的初始坐标,
     *   生命周期结束时就可以回到这个位置重新发射。
     *
     * 默认什么都不做。
     */
    virtual void onAttach(Object&) {
    }

    /**
     * 每帧更新钩子
     * @param owner         当前插件所属的 Object
     * @param deltaSeconds  距离上一帧的秒数
     *
     * Object::update() 会依次调用所有插件的 update()。
     * 默认什么都不做。
     */
    virtual void update(Object&, float) {
    }

    /**
     * 时间轴定位钩子
     *
     * 和 update(dt) 不同, setTime() 接收"绝对动画时间"。
     * 子窗口拖动时间轴时会直接调用它, 因此插件可以精确地
     * 跳到任意时间点, 而不是只能向前累加。
     *
     * 默认不处理时间。
     */
    virtual void setTime(Object&, float) {
    }

    /**
     * 生成绘制实例钩子
     * @param owner 当前插件所属的 Object
     * @param out   输出列表, 调用方负责清空
     * @return true 表示该插件已经生成了实例;
     *         false 表示没有生成, Object 可以继续使用默认实例。
     *
     * 只要任意一个插件返回 true, Object 就不再添加默认实例,
     * 因此 ParticlePlugin 和 CoordinateSystemPlugin 都不会额外画一个
     * 多余的"本体"网格。
     *
     * 默认返回 false, 表示不参与实例生成。
     */
    virtual bool collectInstances(const Object&,
                                  std::vector<ObjectInstance>&) const {
        return false;
    }
};

