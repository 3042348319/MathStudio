// ====================================================================
// theme.cpp —— MathStudio 统一 ImGui 主题
//
// 设计:
//   * 窗口背景: 近黑色灰
//   * 按钮: 橙色
//   * 普通文字: 白色
//   * 输入框/子面板: 深灰
//   * 选中项/滑块: 橙色强调
//
// 只影响 ImGui UI; 3D 视口使用自己的 Vulkan 背景和着色器。
// ====================================================================

#include "theme.h"

#include "settings.h"

#include "imgui.h"

#include <algorithm>
#include <vector>

const ImWchar* mathStudioGlyphRanges() {
    static ImVector<ImWchar> ranges;
    if (!ranges.empty()) {
        return ranges.Data;
    }

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());

    // 箭头、几何图形、罗马数字、乘号、全角加号。
    static const ImWchar iconRanges[] = {
        0x00D7, 0x00D7,
        0x2160, 0x2188,
        0x2190, 0x21FF,
        0x25A0, 0x25FF,
        0xFF0B, 0xFF0B,
        0,
    };
    builder.AddRanges(iconRanges);
    builder.BuildRanges(&ranges);
    return ranges.Data;
}

void uiSectionGap(float height) {
    ImGui::Dummy(ImVec2(0.0f, height));
}

void uiSectionTitle(const char* icon, const char* title) {
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    if (appSettings().showIcons && icon != nullptr && icon[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, appSettings().accent);
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
    }

    ImGui::SetWindowFontScale(1.07f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

void uiPageTitle(const char* icon, const char* title) {
    if (appSettings().showIcons && icon != nullptr && icon[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, appSettings().accent);
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 10.0f);
    }

    ImGui::SetWindowFontScale(1.22f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
}

void applyMathStudioTheme() {
    ImGui::StyleColorsDark();

    const AppSettings& settings = appSettings();

    // 允许从窗口四边/内边缘调整大小; 停靠后的参数浮窗
    // 需要从内侧边缘拖动改变宽度/高度。
    ImGui::GetIO().ConfigWindowsResizeFromEdges = true;
    ImGui::GetIO().FontGlobalScale = settings.uiScale;

    ImGuiStyle& style = ImGui::GetStyle();

    // ---- 几何 ----
    style.WindowRounding = settings.windowRounding;
    style.ChildRounding = std::max(0.0f, settings.windowRounding - 2.0f);
    style.FrameRounding = settings.frameRounding;
    style.PopupRounding = std::max(0.0f, settings.windowRounding - 1.0f);
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = settings.frameRounding;
    style.TabRounding = settings.frameRounding;

    style.WindowPadding = ImVec2(settings.windowPadding,
                                 std::max(6.0f, settings.windowPadding - 4.0f));
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(9.0f, settings.itemSpacingY);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 11.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    // ---- 基础颜色 (窗口背景和强调色来自设置) ----
    const auto mix = [](const ImVec4& value, float factor) {
        return ImVec4(std::clamp(value.x * factor, 0.0f, 1.0f),
                      std::clamp(value.y * factor, 0.0f, 1.0f),
                      std::clamp(value.z * factor, 0.0f, 1.0f),
                      value.w);
    };

    const ImVec4 nearBlack = settings.windowBg;
    const ImVec4 deeperBlack = mix(nearBlack, 0.72f);
    const ImVec4 panelGray = mix(nearBlack, 1.45f);
    const ImVec4 inputGray = mix(nearBlack, 1.85f);
    const ImVec4 borderGray = mix(nearBlack, 2.8f);
    const ImVec4 white(0.96f, 0.96f, 0.97f, 1.0f);
    const ImVec4 muted(0.58f, 0.60f, 0.64f, 1.0f);

    const ImVec4 orange = settings.accent;
    const ImVec4 orangeHover(
        std::min(1.0f, orange.x * 1.20f + 0.05f),
        std::min(1.0f, orange.y * 1.20f + 0.05f),
        std::min(1.0f, orange.z * 1.20f + 0.05f), 1.0f);
    const ImVec4 orangeActive(
        std::clamp(orange.x * 0.82f, 0.0f, 1.0f),
        std::clamp(orange.y * 0.82f, 0.0f, 1.0f),
        std::clamp(orange.z * 0.82f, 0.0f, 1.0f), 1.0f);
    const ImVec4 orangeSoft(
        orange.x, orange.y, orange.z, 0.35f);

    style.Colors[ImGuiCol_Text] = white;
    style.Colors[ImGuiCol_TextDisabled] = muted;

    style.Colors[ImGuiCol_WindowBg] = nearBlack;
    style.Colors[ImGuiCol_ChildBg] = deeperBlack;
    style.Colors[ImGuiCol_PopupBg] = nearBlack;
    style.Colors[ImGuiCol_MenuBarBg] = panelGray;
    style.Colors[ImGuiCol_Border] = borderGray;
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    style.Colors[ImGuiCol_FrameBg] = inputGray;
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.18f, 0.19f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.21f, 0.22f, 0.24f, 1.0f);

    style.Colors[ImGuiCol_TitleBg] = panelGray;
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = panelGray;

    style.Colors[ImGuiCol_Button] = orange;
    style.Colors[ImGuiCol_ButtonHovered] = orangeHover;
    style.Colors[ImGuiCol_ButtonActive] = orangeActive;

    style.Colors[ImGuiCol_Header] = ImVec4(0.17f, 0.18f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = orangeSoft;
    style.Colors[ImGuiCol_HeaderActive] = orange;

    style.Colors[ImGuiCol_Separator] = borderGray;
    style.Colors[ImGuiCol_SeparatorHovered] = orangeHover;
    style.Colors[ImGuiCol_SeparatorActive] = orange;

    style.Colors[ImGuiCol_ResizeGrip] = orangeSoft;
    style.Colors[ImGuiCol_ResizeGripHovered] = orangeHover;
    style.Colors[ImGuiCol_ResizeGripActive] = orange;

    style.Colors[ImGuiCol_ScrollbarBg] = deeperBlack;
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.29f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = orangeHover;
    style.Colors[ImGuiCol_ScrollbarGrabActive] = orange;

    style.Colors[ImGuiCol_CheckMark] = orange;
    style.Colors[ImGuiCol_SliderGrab] = orange;
    style.Colors[ImGuiCol_SliderGrabActive] = orangeHover;

    style.Colors[ImGuiCol_Tab] = panelGray;
    style.Colors[ImGuiCol_TabHovered] = orangeHover;
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.19f, 0.20f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_TabDimmed] = deeperBlack;
    style.Colors[ImGuiCol_TabDimmedSelected] = panelGray;

    style.Colors[ImGuiCol_TableHeaderBg] = panelGray;
    style.Colors[ImGuiCol_TableBorderStrong] = borderGray;
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.16f, 0.17f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.018f);

    style.Colors[ImGuiCol_NavHighlight] = orange;
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.62f);

    style.Colors[ImGuiCol_PlotLines] = orange;
    style.Colors[ImGuiCol_PlotLinesHovered] = orangeHover;
    style.Colors[ImGuiCol_PlotHistogram] = orange;
    style.Colors[ImGuiCol_PlotHistogramHovered] = orangeHover;

    style.Colors[ImGuiCol_TextSelectedBg] = orangeSoft;
    style.Colors[ImGuiCol_DragDropTarget] = orange;
}
