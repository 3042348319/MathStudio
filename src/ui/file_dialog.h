// ====================================================================
// file_dialog.h —— Win32 文件选择对话框
//
// 只负责"选择文件路径", 不负责加载模型。
// 这样 OBJ 解析 (Mesh) 和 UI (浮窗) 保持解耦。
// ====================================================================
#pragma once

#include <string>

/**
 * 打开 OBJ 文件选择对话框
 * @return UTF-8 路径; 用户取消时返回空字符串
 */
std::string openObjFileDialog();

/** 打开 .math 工程 */
std::string openMathFileDialog();

/** 打开 MathStudio 配置文件 (.ini/.cfg/.txt) */
std::string openSettingsFileDialog();

/**
 * 保存 .math 工程
 * @param suggestedName 默认文件名 (不含扩展名也可以)
 * @return 用户取消时为空
 */
std::string saveMathFileDialog(const std::string& suggestedName);
