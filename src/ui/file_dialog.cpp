// ====================================================================
// file_dialog.cpp —— Win32 GetOpenFileNameW 封装
// ====================================================================

#include "file_dialog.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commdlg.h>

#include <string>

namespace {

/** UTF-16 -> UTF-8 */
std::string toUtf8(const wchar_t* text) {    if (text == nullptr) {
        return std::string();
    }

    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return std::string();
    }

    std::string result(
        static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1,
                        result.data(), length, nullptr, nullptr);
    return result;
}

/** UTF-8 -> UTF-16 */
std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }

    const int length = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 1) {
        return std::wstring();
    }

    std::wstring result(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1,
                        result.data(), length);
    return result;
}

}  // namespace

std::string openObjFileDialog() {
    wchar_t filePath[MAX_PATH * 4] = {};

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"OBJ 模型 (*.obj)\0*.obj\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile =
        static_cast<DWORD>(sizeof(filePath) / sizeof(filePath[0]));
    dialog.lpstrTitle = L"导入 OBJ 模型";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&dialog)) {
        return std::string();
    }

    return toUtf8(filePath);
}

std::string openMathFileDialog() {
    wchar_t filePath[MAX_PATH * 4] = {};

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"Math 工程 (*.math)\0*.math\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile =
        static_cast<DWORD>(sizeof(filePath) / sizeof(filePath[0]));
    dialog.lpstrTitle = L"打开 .math 工程";
    dialog.lpstrDefExt = L"math";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&dialog)) {
        return std::string();
    }

    return toUtf8(filePath);
}

std::string openSettingsFileDialog() {
    wchar_t filePath[MAX_PATH * 4] = {};

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"MathStudio 配置 (*.ini;*.cfg;*.txt)\0*.ini;*.cfg;*.txt\0"
        L"所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile =
        static_cast<DWORD>(sizeof(filePath) / sizeof(filePath[0]));
    dialog.lpstrTitle = L"导入 MathStudio 配置";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&dialog)) {
        return std::string();
    }

    return toUtf8(filePath);
}

std::string saveMathFileDialog(const std::string& suggestedName) {    wchar_t filePath[MAX_PATH * 4] = {};
    const std::wstring suggested = [&]() {
        std::string name = suggestedName.empty()
                               ? "未命名工程"
                               : suggestedName;
        if (name.size() < 5 ||
            name.substr(name.size() - 5) != ".math") {
            name += ".math";
        }
        return toWide(name);
    }();
    wcsncpy_s(filePath, suggested.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"Math 工程 (*.math)\0*.math\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile =
        static_cast<DWORD>(sizeof(filePath) / sizeof(filePath[0]));
    dialog.lpstrTitle = L"保存 .math 工程";
    dialog.lpstrDefExt = L"math";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                   OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&dialog)) {
        return std::string();
    }

    std::string result = toUtf8(filePath);
    if (result.size() < 5 ||
        result.substr(result.size() - 5) != ".math") {
        result += ".math";
    }
    return result;
}
