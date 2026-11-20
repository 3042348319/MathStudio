// ====================================================================
// sub_window.h —— 子窗口 (对象容器 + Vulkan 3D 视口)
//
// 设计要点:
//   * 每个 SubWindow 内部都有一个 Object 列表 (std::vector<unique_ptr<Object>>);
//   * 通过模板方法 addObject<T>(...) 放入对象; 现在通常直接使用 Object,
//     行为 (粒子/坐标轴/...) 由 Object::addPlugin<T>() 注入;
//   * 子窗口是一个独立的 GLFW + Vulkan 窗口, 使用主程序共享的
//     VkInstance / VkDevice / 队列 / 命令池等资源;
//   * 每个子窗口内部有一个 ImGui 参数浮窗, 可以编辑 Object 参数和
//     插件参数; 浮窗可以收起成箭头, 点击箭头重新展开;
//   * 原生窗口和 Vulkan 细节封装在 Impl 中, 公共头文件保持干净。
// ====================================================================
#pragma once

#include "object.h"
#include "object_hierarchy.h"
#include "viewport_renderer.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct ViewportContext;
class Menu;

/**
 * 子窗口
 * 持有一组 Object, 并用自己的 Vulkan 交换链把对象渲染成 3D 实体
 * (粒子是小立方体, 坐标系是红/绿/蓝三根轴)。
 */
class SubWindow {
public:
    /**
     * 构造子窗口 (GLFW 窗口和 Vulkan 资源在第一次 show() 时创建)
     * @param context 主程序共享的 Vulkan 资源
     * @param title 窗口标题
     * @param width 客户区宽度
     * @param height 客户区高度
     */
    SubWindow(std::shared_ptr<const ViewportContext> context,
              std::string title,
              int width = 640,
              int height = 480);

    ~SubWindow();

    SubWindow(const SubWindow&) = delete;
    SubWindow& operator=(const SubWindow&) = delete;
    SubWindow(SubWindow&&) = delete;
    SubWindow& operator=(SubWindow&&) = delete;

    /** 子窗口标题 */
    const std::string& title() const;
    void setTitle(std::string title);

    /**
     * 以模板方式向子窗口放入一个对象 (可插拔的核心)
     * 用法:
     *   Object& object = subWindow.addObject<Object>(mesh, position, color);
     *   object.addPlugin<ParticlePlugin>(...);
     * @tparam T 对象类型, 必须是 Object 或其派生类型
     * @param args 传给 T 构造函数的参数
     * @return 新对象的引用 (方便继续 addPlugin)
     */
    template <typename T, typename... Args>
    T& addObject(Args&&... args) {
        // 编译期确保 T 是 Object 或其派生类型。
        static_assert(std::is_base_of_v<Object, T>,
                      "T must derive from Object");

        // 在堆上构造对象; unique_ptr 负责最终释放。
        auto object = std::make_unique<T>(std::forward<Args>(args)...);

        // move 之前保存引用, 之后通过它继续 addPlugin。
        T& reference = *object;

        // 所有权转移给 SubWindow。
        objects_.push_back(std::move(object));

        // 资源管理器登记新对象 (默认未分组)。
        hierarchy_.addObject(reference.id());

        // 对象列表变化后刷新窗口标题中的对象数量。
        onObjectsChanged();

        return reference;
    }

    /**
     * 放入一个已经创建好的对象 (运行期动态注入)
     * @return 对象的引用
     * @throw std::invalid_argument object 为空时抛出
     */
    Object& addObject(std::unique_ptr<Object> object);

    /** 按索引删除对象 */
    bool removeObject(std::size_t index);

    /** 清空所有对象 */
    void clearObjects();

    /** 对象数量 */
    std::size_t objectCount() const;

    /** 按索引访问对象 (越界返回 nullptr) */
    Object* objectAt(std::size_t index);
    const Object* objectAt(std::size_t index) const;

    /** 只读对象列表 */
    const std::vector<std::unique_ptr<Object>>& objects() const;

    /** 对象层级 / 组别模型 (资源管理器使用) */
    ObjectHierarchy& hierarchy();
    const ObjectHierarchy& hierarchy() const;

    /** 按稳定 ID 查找对象; 找不到返回 nullptr */
    Object* objectById(ObjectId id);
    const Object* objectById(ObjectId id) const;

    /** 按稳定 ID 查找对象在 objects_ 中的索引; 找不到返回 objectCount() */
    std::size_t indexOfObject(ObjectId id) const;

    /** 相机状态 (工程保存/恢复) */
    ViewportCamera camera() const;
    void setCamera(const ViewportCamera& camera);

    /**
     * 按类型查找第一个对象 (方便从外部拿回插件/参数)
     * @return 找到返回指针, 否则返回 nullptr
     */
    template <typename T>
    T* findObject() {
        for (const auto& object : objects_) {
            if (auto* typed = dynamic_cast<T*>(object.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    /** 更新子窗口中的所有对象 (粒子运动等) */
    void update(float deltaSeconds);

    /** 渲染子窗口的一帧 (3D 场景 + ImGui 参数浮窗; 不可见时直接返回) */
    void render();

    /** 显示 / 隐藏子窗口 (第一次显示时创建 GLFW 窗口和 Vulkan 资源) */
    void show();
    void hide();
    bool isVisible() const;

    /** 刷新窗口标题中的对象数量 */
    void refresh();

    /** 绑定所属工程菜单 (用于 File / Setting 菜单动作) */
    void setMenu(Menu* menu);
    Menu* menu() const;

    /** 收起/展开参数浮窗 (用于从外部切换视图/测试) */
    void setPanelCollapsed(bool collapsed);

private:
    // 对象列表变化后的统一处理 (刷新标题/重置选择)
    void onObjectsChanged();

    // PIMPL: 把 GLFW/Vulkan 细节藏在 .cpp 中
    struct Impl;

    // 子窗口标题
    std::string title_;

    // 子窗口拥有的对象列表
    std::vector<std::unique_ptr<Object>> objects_;

    // 所属工程菜单 (不拥有)
    Menu* menu_ = nullptr;

    // 组别/未分组层级; 只保存 ObjectId
    ObjectHierarchy hierarchy_;

    // 窗口/Vulkan 实现; 声明在最后, 保证先析构对象再销毁窗口
    std::unique_ptr<Impl> impl_;
};
