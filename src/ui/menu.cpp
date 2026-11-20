// ====================================================================
// menu.cpp —— ImGui + Vulkan 菜单 / 对象浏览器 / 参数面板
//
// 文件结构:
//   1. Menu 的构造/析构/基本访问;
//   2. 子窗口列表管理;
//   3. 页面状态切换;
//   4. 三个页面的 ImGui 绘制;
//   5. 参数面板如何根据插件类型生成控件。
// ====================================================================

#include "menu.h"

#include "coordinate_system_plugin.h"
#include "file_dialog.h"
#include "mesh.h"
#include "parameter_plugin.h"
#include "particle_plugin.h"
#include "plugin.h"
#include "project_io.h"
#include "sine_particle_plugin.h"
#include "theme.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>

// ====================================================================
// 一、构造 / 析构 / 基本信息
// ====================================================================

Menu::Menu(std::string title)
    : title_(std::move(title)) {
}

Menu::~Menu() = default;

const std::string& Menu::title() const {
    return title_;
}

void Menu::setTitle(std::string title) {
    title_ = std::move(title);
}

// ====================================================================
// 二、子窗口列表
// ====================================================================

// --------------------------------------------------------------------
// 加入一个已经创建好的 SubWindow
//
// 参数是 unique_ptr, 表示 Menu 接管所有权。
// 如果传入空指针, 说明调用方逻辑有问题, 直接抛异常。
// --------------------------------------------------------------------
SubWindow& Menu::addSubWindow(std::unique_ptr<SubWindow> window) {
    if (!window) {
        throw std::invalid_argument("Menu::addSubWindow: window is null");
    }

    // 先保存裸引用, 因为 move 之后 window 就为空了。
    SubWindow& reference = *window;

    // 所有权转移给 Menu。
    subWindows_.push_back(std::move(window));

    reference.setMenu(this);

    // 列表发生变化, 重置到主页面。
    onSubWindowsChanged();

    return reference;
}

// --------------------------------------------------------------------
// 按索引移除子窗口
//
// begin() + index 需要 ptrdiff_t, 所以先做 static_cast。
// 移除后列表索引会变化, 统一回到主页面。
// --------------------------------------------------------------------
bool Menu::removeSubWindow(std::size_t index) {
    if (index >= subWindows_.size()) {
        return false;
    }

    subWindows_.erase(
        subWindows_.begin() + static_cast<std::ptrdiff_t>(index));

    onSubWindowsChanged();
    return true;
}

std::size_t Menu::subWindowCount() const {
    return subWindows_.size();
}

SubWindow* Menu::subWindowAt(std::size_t index) {
    if (index >= subWindows_.size()) {
        return nullptr;
    }
    return subWindows_[index].get();
}

const SubWindow* Menu::subWindowAt(std::size_t index) const {
    if (index >= subWindows_.size()) {
        return nullptr;
    }
    return subWindows_[index].get();
}

const std::vector<std::unique_ptr<SubWindow>>& Menu::subWindows() const {
    return subWindows_;
}

// ====================================================================
// 二点五、工程保存 / 打开 / 新建
// ====================================================================

void Menu::setViewportContext(
    std::shared_ptr<const ViewportContext> context) {
    viewportContext_ = std::move(context);
}

std::shared_ptr<const ViewportContext> Menu::viewportContext() const {
    return viewportContext_;
}

const std::string& Menu::projectName() const {
    return projectName_;
}

void Menu::setProjectName(std::string name) {
    projectName_ = std::move(name);
}

const std::string& Menu::projectPath() const {
    return projectPath_;
}

void Menu::setProjectPath(std::string path) {
    projectPath_ = std::move(path);
}

ProjectHistory& Menu::history() {
    return history_;
}

void Menu::clearSubWindows() {
    subWindows_.clear();
    currentSubWindow_ = 0;
    currentObject_ = 0;
    page_ = Page::Main;
}

void Menu::newProject(const std::string& name) {
    clearSubWindows();

    projectName_ = name.empty() ? "未命名工程" : name;
    projectPath_.clear();
    statusMessage_.clear();
    projectDirty_ = false;

    if (!viewportContext_) {
        statusMessage_ = "渲染上下文尚未初始化";
        return;
    }

    auto window = std::make_unique<SubWindow>(
        viewportContext_, projectName_, 640, 480);
    SubWindow& reference = addSubWindow(std::move(window));

    try {
        Mesh cube = Mesh::loadFromResource("cube.mesh");
        reference.addObject<Object>(
            std::move(cube), glm::vec3(0.0f),
            glm::vec4(0.70f, 0.78f, 1.0f, 1.0f), "立方体");
    } catch (const std::exception& error) {
        statusMessage_ = error.what();
    }

    openSubWindow(0);
    projectDirty_ = false;
}

bool Menu::saveProject(const std::string& path, std::string& error) {
    if (path.empty()) {
        error = "工程路径为空";
        return false;
    }

    if (!saveMathProject(*this, path, error)) {
        return false;
    }

    projectPath_ = path;
    history_.add(path);
    statusMessage_ = "工程已保存: " + path;
    projectDirty_ = false;
    return true;
}

bool Menu::openProject(const std::string& path, std::string& error) {
    if (path.empty()) {
        error = "工程路径为空";
        return false;
    }

    if (!loadMathProject(*this, path, error)) {
        return false;
    }

    projectPath_ = path;
    history_.add(path);
    statusMessage_ = "工程已打开: " + path;
    projectDirty_ = false;
    return true;
}

void Menu::markProjectDirty() {
    projectDirty_ = true;
}

bool Menu::isProjectDirty() const {
    return projectDirty_;
}

void Menu::clearProjectDirty() {
    projectDirty_ = false;
}

// ====================================================================
// 三、显示 / 更新 / 渲染
// ====================================================================

void Menu::show() {
    uiVisible_ = true;
}

void Menu::hide() {
    uiVisible_ = false;
}

bool Menu::isVisible() const {
    return uiVisible_;
}

// --------------------------------------------------------------------
// 更新所有子窗口
//
// SubWindow::update() 会遍历自己的 Object 列表,
// Object::update() 再遍历插件。
// 所以这一行最终会驱动所有 ParticlePlugin。
// --------------------------------------------------------------------
void Menu::update(float deltaSeconds) {
    for (const auto& window : subWindows_) {
        window->update(deltaSeconds);
    }
}

// --------------------------------------------------------------------
// 渲染所有子窗口
//
// 每个 SubWindow 有独立的交换链和渲染器;
// 不可见的子窗口会直接跳过。
// --------------------------------------------------------------------
void Menu::render() {
    for (const auto& window : subWindows_) {
        window->render();
    }

    // File 菜单动作必须等所有子窗口渲染完成后执行, 否则
    // open/new/close 会在 SubWindow::renderFrame() 内销毁自身。
    processPendingProjectCommand();
}

void Menu::requestProjectCommand(ProjectCommand command) {
    pendingCommand_ = command;
}

void Menu::processPendingProjectCommand() {
    const ProjectCommand command = pendingCommand_;
    pendingCommand_ = ProjectCommand::None;
    if (command == ProjectCommand::None) {
        return;
    }

    std::string error;

    switch (command) {
    case ProjectCommand::None:
        break;

    case ProjectCommand::New:
        newProject("未命名工程");
        break;

    case ProjectCommand::Open: {
        const std::string path = openMathFileDialog();
        if (!path.empty() && !openProject(path, error)) {
            statusMessage_ = error;
        }
        break;
    }

    case ProjectCommand::Save: {
        std::string path = projectPath_;
        if (path.empty()) {
            path = saveMathFileDialog(projectName_);
        }
        if (!path.empty() && !saveProject(path, error)) {
            statusMessage_ = error;
        }
        break;
    }

    case ProjectCommand::SaveAs: {
        const std::string path = saveMathFileDialog(projectName_);
        if (!path.empty() && !saveProject(path, error)) {
            statusMessage_ = error;
        }
        break;
    }

    case ProjectCommand::Close:
        clearSubWindows();
        projectName_ = "工程";
        projectPath_.clear();
        break;
    }
}

void Menu::openSubWindow(std::size_t index) {
    SubWindow* window = subWindowAt(index);
    if (window != nullptr) {
        window->show();
    }
}

void Menu::closeSubWindow(std::size_t index) {
    SubWindow* window = subWindowAt(index);
    if (window != nullptr) {
        window->hide();
    }
}

void Menu::closeAllSubWindows() {
    for (const auto& window : subWindows_) {
        window->hide();
    }
}

void Menu::refresh() {
    // ImGui 是即时模式, 数据变化会在下一帧自动反映。
}

// ====================================================================
// 四、页面状态
// ====================================================================

void Menu::onSubWindowsChanged() {
    page_ = Page::Main;
    currentSubWindow_ = 0;
    currentObject_ = 0;
}

SubWindow* Menu::currentSubWindowPtr() {
    if (currentSubWindow_ >= subWindows_.size()) {
        return nullptr;
    }
    return subWindows_[currentSubWindow_].get();
}

Object* Menu::currentObjectPtr() {
    // 先拿当前子窗口; 没有就返回空。
    SubWindow* window = currentSubWindowPtr();
    if (window == nullptr) {
        return nullptr;
    }

    // 再检查 Object 索引是否越界。
    if (currentObject_ >= window->objectCount()) {
        return nullptr;
    }

    return window->objectAt(currentObject_);
}

void Menu::showMainPage() {
    page_ = Page::Main;
    currentObject_ = 0;
}

void Menu::showObjectList(std::size_t index) {
    if (index >= subWindows_.size()) {
        return;
    }

    page_ = Page::Objects;
    currentSubWindow_ = index;
    currentObject_ = 0;
}

void Menu::showObjectParameters(std::size_t index) {
    SubWindow* window = currentSubWindowPtr();
    if (window == nullptr) {
        return;
    }
    if (index >= window->objectCount()) {
        return;
    }

    page_ = Page::Parameters;
    currentObject_ = index;
}

void Menu::goBack() {
    if (page_ == Page::Parameters) {
        // 参数 -> 当前子窗口的 Object 列表
        page_ = Page::Objects;
    } else if (page_ == Page::Objects) {
        // Object 列表 -> 主页面
        showMainPage();
    }
}

// --------------------------------------------------------------------
// 页面标题
//
// Main       : "对象系统菜单 - 子窗口"
// Objects    : "粒子系统 - Object 列表"
// Parameters : "粒子系统 / Object - 参数"
// --------------------------------------------------------------------
std::string Menu::pageTitle() const {
    if (page_ == Page::Main) {
        return "工程";
    }

    // 当前子窗口可能已被删除, 所以先检查索引。
    const SubWindow* window = currentSubWindow_ < subWindows_.size()
                                  ? subWindows_[currentSubWindow_].get()
                                  : nullptr;

    if (window == nullptr) {
        return title_;
    }

    if (page_ == Page::Objects) {
        return window->title() + " - Object 列表";
    }

    // 参数页: 显示子窗口 / 对象类型。
    const Object* object =
        currentObject_ < window->objectCount()
            ? window->objectAt(currentObject_)
            : nullptr;

    return window->title() + " / " +
           (object ? object->typeName() : std::string("Object")) +
           " - 参数";
}

// --------------------------------------------------------------------
// 生成主页面的一行子窗口文本
//
// 例如:
//   [0] 粒子系统  [已打开]  对象: 5
// --------------------------------------------------------------------
std::string Menu::buildWindowLabel(std::size_t index) const {
    if (index >= subWindows_.size()) {
        return std::string();
    }

    const SubWindow& window = *subWindows_[index];

    std::ostringstream out;
    out << '[' << index << "] " << window.title()
        << (window.isVisible() ? "  [已打开]" : "  [已关闭]")
        << "  对象: " << window.objectCount();

    return out.str();
}

// ====================================================================
// 五、ImGui 总入口
// ====================================================================

void Menu::drawImGui() {
    // F1 隐藏菜单后, 这里直接返回;
    // 主窗口仍然会清屏并呈现一帧空画面。
    if (!uiVisible_) {
        return;
    }

    // ---- 让菜单铺满主窗口客户区 ----
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    // 背景颜色由统一主题提供 (近黑灰色), 这里只调整内边距。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(18.0f, 18.0f));

    // 去掉标题栏/边框/移动/缩放, 让它看起来像一个全屏页面。
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##ObjectSystemMenu", nullptr, flags);

    // ---- 顶部返回按钮 ----
    // 主页面没有上一级, 所以不显示。
    if (page_ != Page::Main) {
        if (ImGui::Button("← 返回", ImVec2(96.0f, 34.0f))) {
            goBack();
        }
        ImGui::SameLine();
    }

    // ---- 页面标题 ----
    uiPageTitle("◆", pageTitle().c_str());

    // ---- 根据当前页面调用对应绘制函数 ----
    switch (page_) {
    case Page::Main:
        drawMainPage();
        break;

    case Page::Objects:
        drawObjectsPage();
        break;

    case Page::Parameters:
        drawParametersPage();
        break;
    }

    ImGui::End();

    // 与前面的 Push 一一对应, 必须成对弹出。
    ImGui::PopStyleVar();
}

// ====================================================================
// 六、主页面: 子窗口列表
// ====================================================================

void Menu::drawMainPage() {
    // ---- 当前工程 ----
    uiSectionTitle("◆", "当前工程");

    if (!projectName_.empty() && projectName_ != "工程") {
        ImGui::SetWindowFontScale(1.12f);
        ImGui::TextUnformatted(projectName_.c_str());
        ImGui::SetWindowFontScale(1.0f);
    } else {
        ImGui::TextDisabled("未命名工程");
    }

    if (projectPath_.empty()) {
        // 未保存工程不额外显示说明文字。
    } else {
        ImGui::TextDisabled("%s", projectPath_.c_str());
    }

    uiSectionGap(8.0f);

    // ---- 文件操作 ----
    uiSectionTitle("◆", "工程操作");

    if (ImGui::Button("＋ 新建工程", ImVec2(112.0f, 32.0f))) {
        std::snprintf(newProjectName_, sizeof(newProjectName_), "%s",
                      "未命名工程");
        showNewProjectPopup_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("→ 打开工程", ImVec2(112.0f, 32.0f))) {
        const std::string path = openMathFileDialog();
        if (!path.empty()) {
            std::string error;
            if (!openProject(path, error)) {
                statusMessage_ = error;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("↓ 保存工程", ImVec2(112.0f, 32.0f))) {
        std::string path = projectPath_;
        if (path.empty()) {
            path = saveMathFileDialog(projectName_);
        }
        if (!path.empty()) {
            std::string error;
            if (!saveProject(path, error)) {
                statusMessage_ = error;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("↓ 另存为...", ImVec2(112.0f, 32.0f))) {
        const std::string path = saveMathFileDialog(projectName_);
        if (!path.empty()) {
            std::string error;
            if (!saveProject(path, error)) {
                statusMessage_ = error;
            }
        }
    }

    if (!statusMessage_.empty()) {
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }

    uiSectionGap(12.0f);

    // ---- 工程列表 (不再区分场景/子窗口) ----
    uiSectionTitle("◆", "历史工程");
    ImGui::BeginChild("##project_list", ImVec2(0.0f, 0.0f), true);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    std::string pendingOpen;
    std::size_t index = 0;
    for (const std::string& path : history_.entries()) {
        ImGui::PushID(static_cast<int>(index++));

        std::error_code error;
        const bool exists = std::filesystem::exists(
                                std::filesystem::u8path(path), error) &&
                            !error;
        const std::string fileName =
            std::filesystem::u8path(path).filename().u8string();

        if (!exists) {
            ImGui::TextDisabled("%s", fileName.c_str());
        } else {
            if (ImGui::Selectable(fileName.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, 30.0f))) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    pendingOpen = path;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("→ 打开")) {
                pendingOpen = path;
            }
            ImGui::TextDisabled("  %s", path.c_str());
        }

        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    if (history_.entries().empty()) {
        // 空工程列表保持纯空白, 不显示提示文字。
    }

    ImGui::EndChild();

    // ---- 新建工程弹窗 ----
    if (showNewProjectPopup_) {
        ImGui::OpenPopup("新建工程");
        showNewProjectPopup_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f),
                             ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("新建工程", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        uiSectionTitle("＋", "创建工程");
        uiSectionGap(8.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##new_project_name", newProjectName_,
                         sizeof(newProjectName_));
        uiSectionGap(8.0f);

        if (ImGui::Button("＋ 创建", ImVec2(110.0f, 32.0f))) {
            newProject(newProjectName_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("× 取消", ImVec2(110.0f, 32.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // 打开操作会清空并重建菜单容器，因此放在列表遍历结束后处理。
    if (!pendingOpen.empty()) {
        std::string error;
        if (!openProject(pendingOpen, error)) {
            statusMessage_ = error;
        }
    }
}

// ====================================================================
// 七、Object 列表页
// ====================================================================

void Menu::drawObjectsPage() {
    SubWindow* window = currentSubWindowPtr();

    // 当前子窗口被删除/索引失效时, 自动回到主页面。
    if (window == nullptr) {
        showMainPage();
        return;
    }

    uiSectionTitle("◆", "对象列表");
    uiSectionGap(6.0f);

    const float footerHeight = 48.0f;

    ImGui::BeginChild("##object_list",
                      ImVec2(0, -footerHeight),
                      true);

    for (std::size_t i = 0; i < window->objectCount(); ++i) {
        const Object* object = window->objectAt(i);
        if (object == nullptr) {
            continue;
        }

        // 列表文本:
        //   [0] Object  mesh=cube  verts=24  plugins=3
        std::ostringstream label;
        label << '[' << i << "] " << object->typeName()
              << "  mesh=" << object->mesh().name()
              << "  verts=" << object->mesh().vertices().size()
              << "  plugins=" << object->pluginCount();

        ImGui::PushID(static_cast<int>(i));

        const bool selected = (i == currentObject_);

        if (ImGui::Selectable(label.str().c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(0.0f, 32.0f))) {
            showObjectParameters(i);
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    // 底部按钮与主页面一致, 方便在 Object 列表里直接控制渲染窗口。
    if (ImGui::Button("→ 打开渲染", ImVec2(120.0f, 34.0f))) {
        openSubWindow(currentSubWindow_);
    }

    ImGui::SameLine();

    if (ImGui::Button("■ 关闭渲染", ImVec2(120.0f, 34.0f))) {
        closeSubWindow(currentSubWindow_);
    }

    ImGui::SameLine();

    if (ImGui::Button("× 全部关闭", ImVec2(120.0f, 34.0f))) {
        closeAllSubWindows();
    }
}

// ====================================================================
// 八、参数页
// ====================================================================

void Menu::drawParametersPage() {
    Object* object = currentObjectPtr();

    // 当前 Object 不存在时:
    //   * 子窗口还在 -> 回到 Object 列表;
    //   * 子窗口也没了 -> 回到主页面。
    if (object == nullptr) {
        if (currentSubWindowPtr() != nullptr) {
            page_ = Page::Objects;
        } else {
            showMainPage();
        }
        return;
    }

    uiSectionTitle("◆", "对象参数");
    uiSectionGap(6.0f);

    // 参数面板占满剩余高度, 内容过多时可滚动。
    ImGui::BeginChild("##parameter_panel", ImVec2(0, 0), true);

    // ---- 网格信息 (只读) ----
    ImGui::Text("网格: %s", object->mesh().name().c_str());

    ImGui::TextDisabled("顶点: %zu    索引: %zu",
                        object->mesh().vertices().size(),
                        object->mesh().indices().size());

    ImGui::Spacing();

    // ---- 坐标 ----
    // ImGui 的 DragFloat3 需要一个 float[3],
    // 所以先从 Object 拷贝出来, 编辑后再写回。
    float position[3] = {
        object->position().x,
        object->position().y,
        object->position().z
    };

    if (ImGui::DragFloat3("坐标", position, 0.01f)) {
        object->setPosition(
            glm::vec3(position[0], position[1], position[2]));
    }

    // ---- 颜色 ----
    float color[4] = {
        object->color().r,
        object->color().g,
        object->color().b,
        object->color().a
    };

    if (ImGui::ColorEdit4("颜色", color,
                          ImGuiColorEditFlags_AlphaBar)) {
        object->setColor(
            glm::vec4(color[0], color[1], color[2], color[3]));
    }

    // ---- 插件区域 ----
    const auto& plugins = object->plugins();

    if (!plugins.empty()) {
        uiSectionTitle("◆", "插件");

        for (std::size_t i = 0; i < plugins.size(); ++i) {
            // 每个插件一个唯一 ID 作用域, 避免控件名冲突。
            ImGui::PushID(static_cast<int>(i));

            // =========================================================
            // 粒子插件
            // =========================================================
            if (auto* particle =
                    dynamic_cast<ParticlePlugin*>(plugins[i].get())) {
                uiSectionTitle("◆", "粒子");

                // 速度: vec3
                float velocity[3] = {
                    particle->velocity().x,
                    particle->velocity().y,
                    particle->velocity().z
                };

                if (ImGui::DragFloat3("速度", velocity, 0.01f)) {
                    particle->setVelocity(
                        glm::vec3(velocity[0],
                                  velocity[1],
                                  velocity[2]));
                }

                // 生命周期: 0.01 ~ 60 秒
                float lifetime = particle->lifetime();

                if (ImGui::DragFloat("生命周期", &lifetime,
                                     0.05f, 0.01f, 60.0f)) {
                    particle->setLifetime(lifetime);
                }

                // 尺寸: 0 ~ 5
                float size = particle->size();

                if (ImGui::DragFloat("尺寸", &size,
                                     0.005f, 0.0f, 5.0f)) {
                    particle->setSize(size);
                }

                // 当前年龄只读显示。
                ImGui::TextDisabled("当前年龄: %.2f s", particle->age());
            }

            // =========================================================
            // 坐标系插件
            // =========================================================
            else if (auto* coordinate =
                         dynamic_cast<CoordinateSystemPlugin*>(
                             plugins[i].get())) {
                uiSectionTitle("◆", "坐标系");

                float axisLength = coordinate->axisLength();

                if (ImGui::DragFloat("轴长", &axisLength,
                                     0.02f, 0.01f, 20.0f)) {
                    coordinate->setAxisLength(axisLength);
                }

                bool showGrid = coordinate->showGrid();

                if (ImGui::Checkbox("显示网格", &showGrid)) {
                    coordinate->setShowGrid(showGrid);
                }
            }

            // =========================================================
            // 正弦粒子插件
            // =========================================================
            else if (auto* sine =
                         dynamic_cast<SineParticlePlugin*>(
                             plugins[i].get())) {
                uiSectionTitle("◆", "正弦粒子");

                float amplitude = sine->amplitude();
                if (ImGui::DragFloat("振幅", &amplitude,
                                     0.01f, 0.0f, 5.0f)) {
                    sine->setAmplitude(amplitude);
                }

                float pathLength = sine->pathLength();
                if (ImGui::DragFloat("轨道长度", &pathLength,
                                     0.02f, 0.01f, 10.0f)) {
                    sine->setPathLength(pathLength);
                }

                float frequency = sine->frequency();
                if (ImGui::DragFloat("频率", &frequency,
                                     0.02f, -20.0f, 20.0f)) {
                    sine->setFrequency(frequency);
                }

                float speed = sine->speed();
                if (ImGui::DragFloat("速度", &speed,
                                     0.01f, -10.0f, 10.0f)) {
                    sine->setSpeed(speed);
                }

                float phase = sine->phase();
                if (ImGui::DragFloat("相位", &phase,
                                     0.01f, -20.0f, 20.0f)) {
                    sine->setPhase(phase);
                }

                float size = sine->size();
                if (ImGui::DragFloat("粒子尺寸", &size,
                                     0.005f, 0.001f, 2.0f)) {
                    sine->setSize(size);
                }

                float trailDuration = sine->trailDuration();
                if (ImGui::DragFloat("轨迹时长", &trailDuration,
                                     0.01f, 0.0f, 10.0f)) {
                    sine->setTrailDuration(trailDuration);
                }

                int trailSamples = sine->trailSamples();
                if (ImGui::SliderInt("轨迹点数", &trailSamples,
                                     2, 120)) {
                    sine->setTrailSamples(trailSamples);
                }

                float trailSize = sine->trailPointSize();
                if (ImGui::DragFloat("轨迹点大小", &trailSize,
                                     0.001f, 0.001f, 0.5f)) {
                    sine->setTrailPointSize(trailSize);
                }
            }

            // =========================================================
            // 通用参数插件
            // =========================================================
            else if (const auto* parameter =
                         dynamic_cast<const ParameterPlugin*>(
                             plugins[i].get())) {
                // InputText 需要可写的 char 缓冲区,
                // 所以先把 std::string 拷贝到栈上的数组。
                char value[256] = {};

                std::snprintf(value, sizeof(value), "%s",
                              parameter->value().c_str());

                // ### 后面的内容只作为 ImGui ID, 不显示。
                const std::string label =
                    parameter->key() + "###plugin_value";

                if (ImGui::InputText(label.c_str(), value,
                                     sizeof(value))) {
                    // 输入变化后写回插件。
                    object->setParameter(
                        "plugin." + parameter->key(), value);
                }
            }

            ImGui::PopID();
        }
    }

    ImGui::EndChild();
}

