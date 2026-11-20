// ====================================================================
// project_io.h —— .math 工程序列化
//
// 一个工程包含:
//   * 所有子窗口;
//   * 每个子窗口的相机;
//   * 每个 Object 的名称 / 坐标 / 颜色 / 完整网格;
//   * Object 的所有插件参数;
//   * 资源管理器的组别与对象归属。
//
// 数据先写成二进制流, 再用 compression.cpp 压缩进 .math 容器。
// ====================================================================
#pragma once

#include <string>

class Menu;

bool saveMathProject(const Menu& menu,
                     const std::string& path,
                     std::string& error);

bool loadMathProject(Menu& menu,
                     const std::string& path,
                     std::string& error);
