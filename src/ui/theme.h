// ====================================================================
// theme.h —— MathStudio 统一 ImGui 主题
// ====================================================================
#pragma once

#include "imgui.h"

/** 将当前 ImGui 上下文设置为 MathStudio 深色橙色主题 */
void applyMathStudioTheme();

/**
 * 中文字体 + UI 图标字符范围
 * 包含箭头、几何图形、全角加号等, 避免图标显示成问号。
 */
const ImWchar* mathStudioGlyphRanges();

/** 区块之间的大段留白, 代替分隔线 */
void uiSectionGap(float height = 14.0f);

/** 带图标的小节标题; 字体略大, 与正文形成层级 */
void uiSectionTitle(const char* icon, const char* title);

/** 页面主标题: 大号字 + 图标 */
void uiPageTitle(const char* icon, const char* title);
