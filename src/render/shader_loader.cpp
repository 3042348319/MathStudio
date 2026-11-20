// ====================================================================
// shader_loader.cpp —— SPIR-V 着色器文件读取实现
// ====================================================================

#include "shader_loader.h"

#include <fstream>
#include <stdexcept>

std::vector<char> loadShaderCode(const std::string& name) {
    // 候选路径列表, 按优先级排列
    std::vector<std::string> candidates = {
        "shaders/" + name,
        name,
        "../shaders/" + name,
        "../../shaders/" + name,
        "../../../shaders/" + name,
    };
    for (const std::string& path : candidates) {
        // ios::ate: 打开后立即定位到文件末尾, 方便获取文件大小
        // ios::binary: 二进制模式, 避免 Windows 下换行符被转换
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open())
            continue;  // 该路径不存在, 尝试下一个

        // tellg() 返回当前读取位置 = 文件末尾 = 文件大小
        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        // 回到文件开头, 一次性读出全部内容
        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();
        return buffer;
    }
    // 所有路径都找不到, 抛出异常 (程序无法继续)
    throw std::runtime_error(
        "找不到着色器文件 " + name +
        "! 请确认运行目录中包含 shaders 目录 (可用 VS 调试或从 build 目录运行)");
}
