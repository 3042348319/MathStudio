// ====================================================================
// settings.h —— MathStudio 快捷键 / UI 设置
// ====================================================================
#pragma once

#include "imgui.h"

#include <filesystem>
#include <string>

/** 全局设置: 快捷键 + ImGui 主题细节 */
struct AppSettings {
    // ---- 快捷键 (字符串格式: Ctrl+Shift+S) ----
    std::string saveShortcut = "Ctrl+S";
    std::string saveAsShortcut = "Ctrl+Shift+S";
    std::string openShortcut = "Ctrl+O";
    std::string newShortcut = "Ctrl+N";
    std::string closeShortcut = "Ctrl+W";
    std::string settingsShortcut = "Ctrl+,";
    std::string editorShortcut = "Ctrl+`";
    std::string timelineShortcut = "T";

    // ---- UI ----
    ImVec4 accent{0.91f, 0.46f, 0.09f, 1.0f};
    ImVec4 windowBg{0.072f, 0.075f, 0.082f, 1.0f};
    float panelOpacity = 0.94f;
    float windowRounding = 9.0f;
    float frameRounding = 6.0f;
    float itemSpacingY = 8.0f;
    float windowPadding = 14.0f;
    float uiScale = 1.0f;
    bool showIcons = true;
};

AppSettings& appSettings();

/** 设置修改计数; 各 ImGui 上下文据此重新应用主题 */
unsigned uiSettingsRevision();
void markUiSettingsChanged();
void resetAppSettings();

/** 默认 settings.ini 路径 (当前工作目录) */
std::filesystem::path defaultSettingsPath();

/**
 * 读取配置。支持 [shortcuts] / [ui] 两节, 也接受不带节名的
 * key=value。格式错误时返回 false, 不修改当前设置。
 */
bool loadSettingsFile(const std::string& path, std::string& error);
bool saveSettingsFile(const std::string& path, std::string& error);

/** 判断当前 GLFW 按键/修饰键是否匹配配置字符串 */
bool shortcutMatches(const std::string& shortcut, int key, int mods);
