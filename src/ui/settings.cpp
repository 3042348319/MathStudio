// ====================================================================
// settings.cpp —— MathStudio 快捷键 / UI 配置
// ====================================================================

#include "settings.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

AppSettings g_settings;
unsigned g_revision = 1;

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) {
        return std::isspace(ch) == 0;
    };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(),
                value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

bool parseFloat(const std::string& text, float& out) {
    try {
        std::size_t used = 0;
        const float value = std::stof(text, &used);
        if (used != text.size() || !std::isfinite(value)) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

int hexDigit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool parseColor(const std::string& text, ImVec4& out) {
    std::string value = trim(text);
    if (!value.empty() && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    if (!value.empty() && value.front() == '#') {
        value.erase(value.begin());
    }
    if (value.size() != 6) {
        return false;
    }

    int rgb[3] = {};
    for (int i = 0; i < 3; ++i) {
        const int high = hexDigit(value[i * 2]);
        const int low = hexDigit(value[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        rgb[i] = high * 16 + low;
    }

    out = ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f,
                 rgb[2] / 255.0f, 1.0f);
    return true;
}

std::string colorToHex(const ImVec4& color) {
    const auto channel = [](float value) {
        const int v = static_cast<int>(std::clamp(value, 0.0f, 1.0f) *
                                       255.0f + 0.5f);
        const char* digits = "0123456789ABCDEF";
        std::string result;
        result += digits[(v >> 4) & 0xF];
        result += digits[v & 0xF];
        return result;
    };
    return "#" + channel(color.x) + channel(color.y) + channel(color.z);
}

int keyFromToken(const std::string& rawToken) {
    const std::string token = lower(trim(rawToken));
    if (token.empty()) {
        return -1;
    }
    if (token.size() == 1) {
        const char ch = static_cast<char>(std::toupper(
            static_cast<unsigned char>(token[0])));
        if (ch >= 'A' && ch <= 'Z') {
            return GLFW_KEY_A + (ch - 'A');
        }
        if (ch >= '0' && ch <= '9') {
            return GLFW_KEY_0 + (ch - '0');
        }
        switch (token[0]) {
        case ',': return GLFW_KEY_COMMA;
        case '.': return GLFW_KEY_PERIOD;
        case '-': return GLFW_KEY_MINUS;
        case '=': return GLFW_KEY_EQUAL;
        case ';': return GLFW_KEY_SEMICOLON;
        case '/': return GLFW_KEY_SLASH;
        case '\\': return GLFW_KEY_BACKSLASH;
        case '`': return GLFW_KEY_GRAVE_ACCENT;
        default: return -1;
        }
    }
    if (token.size() >= 2 && token[0] == 'f') {
        try {
            const int number = std::stoi(token.substr(1));
            if (number >= 1 && number <= 12) {
                return GLFW_KEY_F1 + (number - 1);
            }
        } catch (...) {
        }
        return -1;
    }
    if (token == "space") return GLFW_KEY_SPACE;
    if (token == "tab") return GLFW_KEY_TAB;
    if (token == "escape" || token == "esc") return GLFW_KEY_ESCAPE;
    if (token == "enter" || token == "return") return GLFW_KEY_ENTER;
    if (token == "backspace") return GLFW_KEY_BACKSPACE;
    if (token == "delete" || token == "del") return GLFW_KEY_DELETE;
    if (token == "up") return GLFW_KEY_UP;
    if (token == "down") return GLFW_KEY_DOWN;
    if (token == "left") return GLFW_KEY_LEFT;
    if (token == "right") return GLFW_KEY_RIGHT;
    return -1;
}

std::string quoteIfNeeded(const std::string& value) {
    return value;
}

}  // namespace

AppSettings& appSettings() {
    return g_settings;
}

unsigned uiSettingsRevision() {
    return g_revision;
}

void markUiSettingsChanged() {
    ++g_revision;
}

void resetAppSettings() {
    g_settings = AppSettings{};
    markUiSettingsChanged();
}

std::filesystem::path defaultSettingsPath() {
    return std::filesystem::current_path() / "settings.ini";
}

bool loadSettingsFile(const std::string& path, std::string& error) {
    std::ifstream file(std::filesystem::u8path(path));
    if (!file.is_open()) {
        error = "无法打开配置文件";
        return false;
    }

    AppSettings next;
    std::string section;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "配置文件第 " + std::to_string(lineNumber) +
                    " 行缺少 '='";
            return false;
        }

        const std::string key = lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' &&
            value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        // shortcuts
        if (section == "shortcuts" || key == "save" ||
            key == "save_as" || key == "open" || key == "new" ||
            key == "close" || key == "settings" ||
            key == "editor" || key == "timeline") {
            if (key == "save") next.saveShortcut = value;
            else if (key == "save_as") next.saveAsShortcut = value;
            else if (key == "open") next.openShortcut = value;
            else if (key == "new") next.newShortcut = value;
            else if (key == "close") next.closeShortcut = value;
            else if (key == "settings") next.settingsShortcut = value;
            else if (key == "editor") next.editorShortcut = value;
            else if (key == "timeline") next.timelineShortcut = value;
            else {
                error = "配置文件第 " + std::to_string(lineNumber) +
                        " 行快捷键未知";
                return false;
            }
            continue;
        }

        // ui
        if (key == "accent" || key == "accent_color") {
            if (!parseColor(value, next.accent)) {
                error = "accent 颜色格式错误";
                return false;
            }
        } else if (key == "window_bg" || key == "background") {
            if (!parseColor(value, next.windowBg)) {
                error = "window_bg 颜色格式错误";
                return false;
            }
        } else if (key == "panel_opacity") {
            if (!parseFloat(value, next.panelOpacity)) {
                error = "panel_opacity 不是数字";
                return false;
            }
            next.panelOpacity = std::clamp(next.panelOpacity, 0.2f, 1.0f);
        } else if (key == "window_rounding") {
            if (!parseFloat(value, next.windowRounding)) {
                error = "window_rounding 不是数字";
                return false;
            }
            next.windowRounding = std::clamp(next.windowRounding, 0.0f, 24.0f);
        } else if (key == "frame_rounding") {
            if (!parseFloat(value, next.frameRounding)) {
                error = "frame_rounding 不是数字";
                return false;
            }
            next.frameRounding = std::clamp(next.frameRounding, 0.0f, 24.0f);
        } else if (key == "item_spacing_y") {
            if (!parseFloat(value, next.itemSpacingY)) {
                error = "item_spacing_y 不是数字";
                return false;
            }
            next.itemSpacingY = std::clamp(next.itemSpacingY, 0.0f, 30.0f);
        } else if (key == "window_padding") {
            if (!parseFloat(value, next.windowPadding)) {
                error = "window_padding 不是数字";
                return false;
            }
            next.windowPadding = std::clamp(next.windowPadding, 4.0f, 40.0f);
        } else if (key == "ui_scale") {
            if (!parseFloat(value, next.uiScale)) {
                error = "ui_scale 不是数字";
                return false;
            }
            next.uiScale = std::clamp(next.uiScale, 0.75f, 1.75f);
        } else if (key == "show_icons") {
            const std::string v = lower(value);
            next.showIcons = !(v == "0" || v == "false" || v == "off");
        }
    }

    g_settings = next;
    markUiSettingsChanged();
    error.clear();
    return true;
}

bool saveSettingsFile(const std::string& path, std::string& error) {
    std::ofstream file(std::filesystem::u8path(path),
                       std::ios::trunc);
    if (!file.is_open()) {
        error = "无法写入配置文件";
        return false;
    }

    file << "[shortcuts]\n";
    file << "save=" << quoteIfNeeded(g_settings.saveShortcut) << "\n";
    file << "save_as=" << quoteIfNeeded(g_settings.saveAsShortcut) << "\n";
    file << "open=" << quoteIfNeeded(g_settings.openShortcut) << "\n";
    file << "new=" << quoteIfNeeded(g_settings.newShortcut) << "\n";
    file << "close=" << quoteIfNeeded(g_settings.closeShortcut) << "\n";
    file << "settings=" << quoteIfNeeded(g_settings.settingsShortcut) << "\n";
    file << "editor=" << quoteIfNeeded(g_settings.editorShortcut) << "\n";
    file << "timeline=" << quoteIfNeeded(g_settings.timelineShortcut) << "\n\n";

    file << "[ui]\n";
    file << "accent=" << colorToHex(g_settings.accent) << "\n";
    file << "window_bg=" << colorToHex(g_settings.windowBg) << "\n";
    file << "panel_opacity=" << g_settings.panelOpacity << "\n";
    file << "window_rounding=" << g_settings.windowRounding << "\n";
    file << "frame_rounding=" << g_settings.frameRounding << "\n";
    file << "item_spacing_y=" << g_settings.itemSpacingY << "\n";
    file << "window_padding=" << g_settings.windowPadding << "\n";
    file << "ui_scale=" << g_settings.uiScale << "\n";
    file << "show_icons=" << (g_settings.showIcons ? 1 : 0) << "\n";

    if (!file.good()) {
        error = "写入配置文件失败";
        return false;
    }
    error.clear();
    return true;
}

bool shortcutMatches(const std::string& shortcut, int key, int mods) {
    std::string spec = trim(shortcut);
    if (spec.empty()) {
        return false;
    }

    int requiredMods = 0;
    int wantedKey = -1;

    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t plus = spec.find('+', start);
        const std::string token = trim(spec.substr(
            start, plus == std::string::npos ? std::string::npos
                                             : plus - start));
        if (!token.empty()) {
            const std::string lowerToken = lower(token);
            if (lowerToken == "ctrl" || lowerToken == "control") {
                requiredMods |= GLFW_MOD_CONTROL;
            } else if (lowerToken == "shift") {
                requiredMods |= GLFW_MOD_SHIFT;
            } else if (lowerToken == "alt") {
                requiredMods |= GLFW_MOD_ALT;
            } else {
                wantedKey = keyFromToken(token);
            }
        }
        if (plus == std::string::npos) {
            break;
        }
        start = plus + 1;
    }

    if (wantedKey < 0 || wantedKey != key) {
        return false;
    }

    const int relevantMods =
        mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT);
    return relevantMods == requiredMods;
}
