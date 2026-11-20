// ====================================================================
// app_icon.h —— GLFW 窗口图标
// ====================================================================
#pragma once

struct AppIconImage {
    int width = 0;
    int height = 0;
    const unsigned char* pixels = nullptr;
};

struct GLFWwindow;

/** 返回图标数组 (RGBA, 从上到下); count 为数量 */
const AppIconImage* appIconImages(int& count);

/** 给 GLFW 窗口设置 MathStudio 图标 */
void applyAppIcon(GLFWwindow* window);

/** 将 Windows 原生标题栏改为深色 (Windows 10/11) */
void applyDarkWindowFrame(GLFWwindow* window);
