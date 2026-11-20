// ====================================================================
// shader_loader.h —— SPIR-V 着色器文件读取
// ====================================================================
#pragma once

#include <string>
#include <vector>

/**
 * 读取着色器 SPIR-V 文件
 * 程序运行时按候选路径依次查找 (对应不同的运行工作目录):
 *   1. "shaders/xxx"       —— 在 build 目录下运行程序时
 *   2. "xxx"               —— VS 按 F5 调试时 (工作目录被设为 build/shaders)
 *   3. "../shaders/xxx"    —— 在 build/Debug 下直接运行 exe 时
 *   4. "../../shaders/xxx" —— 在 build/Release 等更深目录下运行时
 * @param name 着色器文件名, 如 "vert.spv"
 * @return 文件的二进制内容 (SPIR-V 字节码)
 * @throw std::runtime_error 所有候选路径都找不到文件时抛出
 */
std::vector<char> loadShaderCode(const std::string& name);
