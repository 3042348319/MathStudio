// ====================================================================
// menu.h —— 主窗口菜单 / 对象浏览器 / 参数面板
//
// 菜单完全由 ImGui + Vulkan 渲染, 不再创建原生 Win32 子控件。
//
// 页面层级
// --------------------------------------------------------------------
//   Main
//     ├── 子窗口列表
//     └── 打开渲染 / 关闭渲染 / 全部关闭
//
//   Objects
//     ├── 当前子窗口的 Object 列表
//     └── 单击某个 Object -> Parameters
//
//   Parameters
//     ├── 网格信息 (只读)
//     ├── 坐标 / 颜色
//     └── 遍历插件, 按插件类型生成控件
//
// 左上角 "< 返回" 逐级返回:
//   Parameters -> Objects -> Main
// ====================================================================
#pragma once

#include "sub_window.h"
#include "project_history.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * 菜单 / 对象浏览器
 *
 * 职责:
 *   * 持有所有 SubWindow;
 *   * 管理当前页面和当前选中的子窗口/Object;
 *   * 用 ImGui 绘制页面;
 *   * 把用户操作转成 SubWindow / Object / Plugin 的修改。
 *
 * 它不直接创建 Vulkan 对象; ImGui 的 Vulkan 后端在
 * VulkanTutorialApp::initImGui() 中初始化。
 */
class Menu {
public:
    // 创建菜单; title 会显示在页面标题中
    explicit Menu(std::string title = "对象系统菜单");

    ~Menu();

    // 菜单持有 unique_ptr 列表, 禁止拷贝/移动, 避免所有权不清。
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;
    Menu(Menu&&) = delete;
    Menu& operator=(Menu&&) = delete;

    // 菜单标题
    const std::string& title() const;
    void setTitle(std::string title);

    /**
     * 以模板方式创建并加入一个子窗口
     *
     * 用法:
     *   menu.addSubWindow<SubWindow>(context, "粒子系统");
     *
     * @tparam T    子窗口类型, 必须派生自 SubWindow
     * @param args  传给 T 构造函数的参数
     * @return 新子窗口的引用
     */
    template <typename T, typename... Args>
    T& addSubWindow(Args&&... args) {
        // 编译期检查类型安全。
        static_assert(std::is_base_of_v<SubWindow, T>,
                      "T must derive from SubWindow");

        // 创建子窗口并保存裸引用。
        auto window = std::make_unique<T>(std::forward<Args>(args)...);
        T& reference = *window;

        // 所有权转移给 Menu。
        subWindows_.push_back(std::move(window));

        reference.setMenu(this);

        // 子窗口列表变化后, 回到主页面并重置索引。
        onSubWindowsChanged();

        return reference;
    }

    // 加入一个已经创建好的子窗口
    SubWindow& addSubWindow(std::unique_ptr<SubWindow> window);

    // 按索引移除子窗口; 越界返回 false
    bool removeSubWindow(std::size_t index);

    // 子窗口数量
    std::size_t subWindowCount() const;

    // 按索引访问子窗口; 越界返回 nullptr
    SubWindow* subWindowAt(std::size_t index);
    const SubWindow* subWindowAt(std::size_t index) const;

    // 只读子窗口列表
    const std::vector<std::unique_ptr<SubWindow>>& subWindows() const;

    // -----------------------------------------------------------------
    // 工程
    // -----------------------------------------------------------------

    void setViewportContext(
        std::shared_ptr<const ViewportContext> context);
    std::shared_ptr<const ViewportContext> viewportContext() const;

    const std::string& projectName() const;
    void setProjectName(std::string name);

    const std::string& projectPath() const;
    void setProjectPath(std::string path);

    ProjectHistory& history();

    /** 清空所有子窗口 (新建/打开工程时) */
    void clearSubWindows();

    /** 新建一个空工程 (含一个默认立方体) */
    void newProject(const std::string& name);

    bool saveProject(const std::string& path, std::string& error);
    bool openProject(const std::string& path, std::string& error);

    /** 工程内容发生修改 */
    void markProjectDirty();
    bool isProjectDirty() const;
    void clearProjectDirty();

    /** 工程窗口菜单栏的延迟动作 (避免在子窗口渲染中销毁自己) */
    enum class ProjectCommand {
        None,
        New,
        Open,
        Save,
        SaveAs,
        Close,
    };

    void requestProjectCommand(ProjectCommand command);

    // -----------------------------------------------------------------
    // 菜单显示/隐藏
    // -----------------------------------------------------------------

    // 显示菜单 UI
    void show();

    // 隐藏菜单 UI (F1 切换时使用)
    void hide();

    // 当前是否显示
    bool isVisible() const;

    // -----------------------------------------------------------------
    // 每帧更新/渲染
    // -----------------------------------------------------------------

    // 更新所有子窗口中的所有 Object; 插件会在这里运动
    void update(float deltaSeconds);

    // 渲染所有可见子窗口 (每个子窗口有自己的 Vulkan 交换链)
    void render();

    /**
     * 用 ImGui 绘制菜单
     * 必须在 ImGui::NewFrame() 之后、ImGui::Render() 之前调用。
     */
    void drawImGui();

    // -----------------------------------------------------------------
    // 子窗口打开/关闭
    // -----------------------------------------------------------------

    void openSubWindow(std::size_t index);
    void closeSubWindow(std::size_t index);
    void closeAllSubWindows();

    // ImGui 是即时模式, 数据变化下一帧自动反映; 保留此接口兼容旧代码
    void refresh();

private:
    // 当前页面
    enum class Page {
        Main,        // 子窗口列表
        Objects,     // 当前子窗口的 Object 列表
        Parameters,  // 当前 Object 的参数面板
    };

    // 子窗口列表变化后的统一处理
    void onSubWindowsChanged();

    // 生成 " [0] 粒子系统 [已打开] 对象: 5 " 这样的列表文本
    std::string buildWindowLabel(std::size_t index) const;

    // 生成当前页面标题
    std::string pageTitle() const;

    // 执行延迟的 File 菜单动作
    void processPendingProjectCommand();

    // 页面切换
    void showMainPage();
    void showObjectList(std::size_t index);
    void showObjectParameters(std::size_t index);
    void goBack();

    // 三个页面的绘制函数
    void drawMainPage();
    void drawObjectsPage();
    void drawParametersPage();

    // 当前选中的子窗口/Object; 越界返回 nullptr
    SubWindow* currentSubWindowPtr();
    Object* currentObjectPtr();

    // 菜单标题
    std::string title_;

    // 所有子窗口
    std::vector<std::unique_ptr<SubWindow>> subWindows_;

    // 菜单 UI 是否可见
    bool uiVisible_ = true;

    // 当前页面
    Page page_ = Page::Main;

    // 当前选中的子窗口索引
    std::size_t currentSubWindow_ = 0;

    // 当前选中的 Object 索引
    std::size_t currentObject_ = 0;

    // ---- 工程状态 ----
    std::shared_ptr<const ViewportContext> viewportContext_;
    std::string projectName_ = "未命名工程";
    std::string projectPath_;
    ProjectHistory history_;

    // 新建工程弹窗
    bool showNewProjectPopup_ = false;
    char newProjectName_[128] = "未命名工程";

    // 最近一次文件操作的状态提示
    std::string statusMessage_;

    // 工程窗口 File 菜单的延迟动作
    ProjectCommand pendingCommand_ = ProjectCommand::None;

    // 工程内容是否有未保存修改
    bool projectDirty_ = false;
};
