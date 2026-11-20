// ====================================================================
// mesh.cpp —— 网格加载与 Vulkan 顶点布局
//
// 这个文件做三件事:
//   1. 解析 .mesh 文本文件;
//   2. 在资源目录中查找模型;
//   3. 描述顶点在显存中的排布, 供图形管线使用。
// ====================================================================

#include "mesh.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
// --------------------------------------------------------------------
// 构造函数
//
// 参数按值传入, 然后 std::move 到成员:
//   * 调用方传左值时: 先拷贝一份, 再移动进成员;
//   * 调用方传右值时: 直接移动, 没有额外拷贝。
// --------------------------------------------------------------------
Mesh::Mesh(std::string name,
           std::vector<MeshVertex> vertices,
           std::vector<uint32_t> indices)
    : name_(std::move(name)),
      vertices_(std::move(vertices)),
      indices_(std::move(indices)) {
}

// --------------------------------------------------------------------
// 从文件加载网格
//
// 解析规则:
//   * 空行 / 以 # 开头的行: 跳过;
//   * v 开头: 读取 8 个浮点数, 组成一个 MeshVertex;
//   * f 开头: 读取 3 个整数, 组成一个三角形;
//   * 其他行: 视为格式错误, 直接抛异常。
//
// 之所以严格报错, 是为了让模型路径/格式问题在启动阶段就暴露,
// 而不是等到渲染时得到一张空画面。
// --------------------------------------------------------------------
Mesh Mesh::loadFromFile(const std::string& path) {
    // 打开文件; 失败说明路径错误或文件不存在。
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("找不到网格文件: " + path);
    }

    // 先创建空 Mesh, 边解析边填充。
    Mesh mesh;

    // 网格名取文件名去掉扩展名:
    //   "resources/models/cube.mesh" -> "cube"
    mesh.name_ = std::filesystem::path(path).stem().string();

    // 当前行号和整行文本。
    std::string line;
    int lineNumber = 0;

    // std::getline 会逐行读取, 自动去掉换行符。
    while (std::getline(file, line)) {
        ++lineNumber;

        // 找到第一个非空白字符。
        const auto first = line.find_first_not_of(" \t\r\n");

        // 空行, 或第一个非空白字符是 '#', 都是注释/空行。
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }

        // 用字符串流解析这一行, 先读行首标记 v/f。
        std::istringstream stream(line);
        char tag = 0;
        stream >> tag;

        if (tag == 'v') {
            // ---- 顶点行 ----
            MeshVertex vertex{};
            if (!(stream >> vertex.position.x >> vertex.position.y >>
                  vertex.position.z >> vertex.color.r >> vertex.color.g >>
                  vertex.color.b >> vertex.texCoord.x >> vertex.texCoord.y)) {
                throw std::runtime_error(
                    "网格顶点格式错误: " + path + " 第 " +
                    std::to_string(lineNumber) + " 行");
            }
            mesh.vertices_.push_back(vertex);
        } else if (tag == 'f') {
            // ---- 三角形索引行 ----
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t c = 0;
            if (!(stream >> a >> b >> c)) {
                throw std::runtime_error(
                    "网格索引格式错误: " + path + " 第 " +
                    std::to_string(lineNumber) + " 行");
            }

            // 一个三角形拆成三个索引依次存入,
            // 这样 indices_ 可以直接作为 index buffer。
            mesh.indices_.push_back(a);
            mesh.indices_.push_back(b);
            mesh.indices_.push_back(c);
        } else {
            // 未知标记: 不接受, 避免悄悄忽略错误数据。
            throw std::runtime_error(
                "未知网格行: " + path + " 第 " +
                std::to_string(lineNumber) + " 行");
        }
    }

    // 没有顶点或没有索引, 无法绘制。
    if (mesh.vertices_.empty() || mesh.indices_.empty()) {
        throw std::runtime_error("网格为空: " + path);
    }

    // 每个索引都必须能访问到对应顶点。
    for (uint32_t index : mesh.indices_) {
        if (index >= mesh.vertices_.size()) {
            throw std::runtime_error("网格索引越界: " + path);
        }
    }

    return mesh;
}

// --------------------------------------------------------------------
// 从 Wavefront OBJ 加载
//
// 这里实现的是资源导入所需的最小子集:
//   * v  顶点位置;
//   * vt 纹理坐标 (可选);
//   * f  面, 支持多边形并扇形三角化;
//   * 常见索引形式: v, v/vt, v//vn, v/vt/vn。
//
// OBJ 的顶点/纹理索引从 1 开始, 也允许负数 (相对当前末尾)。
// --------------------------------------------------------------------
Mesh Mesh::loadFromObjFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("找不到 OBJ 文件: " + path);
    }

    Mesh mesh;
    mesh.name_ = std::filesystem::path(path).stem().string();

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;

    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;

        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }

        std::istringstream stream(line);
        std::string tag;
        stream >> tag;

        if (tag == "v") {
            glm::vec3 position{};
            if (!(stream >> position.x >> position.y >> position.z)) {
                throw std::runtime_error(
                    "OBJ 顶点格式错误: " + path + " 第 " +
                    std::to_string(lineNumber) + " 行");
            }
            positions.push_back(position);
        } else if (tag == "vt") {
            glm::vec2 uv{};
            if (!(stream >> uv.x >> uv.y)) {
                throw std::runtime_error(
                    "OBJ 纹理坐标格式错误: " + path + " 第 " +
                    std::to_string(lineNumber) + " 行");
            }
            texCoords.push_back(uv);
        } else if (tag == "f") {
            std::vector<std::uint32_t> faceIndices;
            std::string token;

            // 一个面可能有 3 个或更多顶点; 逐个解析 token。
            while (stream >> token) {
                const std::size_t slash = token.find('/');
                const std::string vertexPart =
                    token.substr(0, slash);

                int vertexIndex = 0;
                try {
                    vertexIndex = std::stoi(vertexPart);
                } catch (...) {
                    throw std::runtime_error(
                        "OBJ 面索引格式错误: " + path + " 第 " +
                        std::to_string(lineNumber) + " 行");
                }

                // OBJ 是 1-based; 负数表示从末尾倒数。
                int resolved = vertexIndex > 0
                                   ? vertexIndex - 1
                                   : static_cast<int>(positions.size()) +
                                         vertexIndex;
                if (resolved < 0 ||
                    resolved >= static_cast<int>(positions.size())) {
                    throw std::runtime_error(
                        "OBJ 面索引越界: " + path + " 第 " +
                        std::to_string(lineNumber) + " 行");
                }

                // 纹理坐标 (可选): token 形如 v/vt 或 v/vt/vn。
                glm::vec2 uv{0.0f};
                if (slash != std::string::npos) {
                    const std::size_t secondSlash =
                        token.find('/', slash + 1);
                    const std::string uvPart = token.substr(
                        slash + 1,
                        secondSlash == std::string::npos
                            ? std::string::npos
                            : secondSlash - slash - 1);

                    if (!uvPart.empty()) {
                        int uvIndex = 0;
                        try {
                            uvIndex = std::stoi(uvPart);
                        } catch (...) {
                            throw std::runtime_error(
                                "OBJ 纹理索引格式错误: " + path +
                                " 第 " + std::to_string(lineNumber) +
                                " 行");
                        }

                        int resolvedUv =
                            uvIndex > 0
                                ? uvIndex - 1
                                : static_cast<int>(texCoords.size()) +
                                      uvIndex;
                        if (resolvedUv >= 0 &&
                            resolvedUv <
                                static_cast<int>(texCoords.size())) {
                            uv = texCoords[resolvedUv];
                        }
                    }
                }

                // 每个面顶点生成一个 MeshVertex。
                // OBJ 的法线/材质暂不导入, 顶点色统一为白色。
                MeshVertex vertex{};
                vertex.position = positions[resolved];
                vertex.color = glm::vec3(1.0f);
                vertex.texCoord = uv;
                faceIndices.push_back(
                    static_cast<std::uint32_t>(mesh.vertices_.size()));
                mesh.vertices_.push_back(vertex);
            }

            if (faceIndices.size() < 3) {
                continue;
            }

            // 多边形 -> 三角形扇形。
            for (std::size_t i = 1; i + 1 < faceIndices.size(); ++i) {
                mesh.indices_.push_back(faceIndices[0]);
                mesh.indices_.push_back(faceIndices[i]);
                mesh.indices_.push_back(faceIndices[i + 1]);
            }
        }
        // o/g/s/usemtl/mtllib 等标记可以安全忽略。
    }

    if (mesh.vertices_.empty() || mesh.indices_.empty()) {
        throw std::runtime_error("OBJ 中没有可用的顶点/面: " + path);
    }

    return mesh;
}

// --------------------------------------------------------------------
// 从 resources/models 加载模型
//
// 程序可能从不同工作目录启动:
//   * 项目根目录:        resources/models/cube.mesh
//   * build 目录:        ../resources/models/cube.mesh
//   * build/shaders:     ../resources/models/cube.mesh
//
// 所以按从近到远的顺序尝试多个候选路径。
// --------------------------------------------------------------------
Mesh Mesh::loadFromResource(const std::string& fileName) {
    const std::vector<std::string> candidates = {
        "resources/models/" + fileName,
        "../resources/models/" + fileName,
        "../../resources/models/" + fileName,
        "../../../resources/models/" + fileName,
        fileName,
    };

    // 找到第一个存在的路径就加载并返回。
    for (const std::string& path : candidates) {
        if (std::filesystem::exists(path)) {
            return loadFromFile(path);
        }
    }

    // 所有候选路径都不存在, 给出明确的错误信息。
    throw std::runtime_error(
        "找不到资源模型 " + fileName +
        "! 请确认 resources/models 目录存在");
}

// --------------------------------------------------------------------
// 读取网格名
// --------------------------------------------------------------------
const std::string& Mesh::name() const {
    return name_;
}

// --------------------------------------------------------------------
// 读取顶点数组 (只读)
// --------------------------------------------------------------------
const std::vector<MeshVertex>& Mesh::vertices() const {
    return vertices_;
}

// --------------------------------------------------------------------
// 读取索引数组 (只读)
// --------------------------------------------------------------------
const std::vector<uint32_t>& Mesh::indices() const {
    return indices_;
}

// --------------------------------------------------------------------
// 是否为空网格
//
// 顶点或索引任意一个为空, 都不能形成可绘制三角形。
// --------------------------------------------------------------------
bool Mesh::empty() const {
    return vertices_.empty() || indices_.empty();
}

// --------------------------------------------------------------------
// 索引数量
//
// vkCmdDrawIndexed 的第一个参数就是它。
// --------------------------------------------------------------------
uint32_t Mesh::indexCount() const {
    return static_cast<uint32_t>(indices_.size());
}

// --------------------------------------------------------------------
// 顶点内存起始地址
//
// std::vector 的元素在内存中连续存放,
// 因此 data() 返回的指针可以直接交给 memcpy / vkCmdCopyBuffer。
// --------------------------------------------------------------------
const void* Mesh::vertexData() const {
    return vertices_.data();
}

// --------------------------------------------------------------------
// 顶点内存字节数
//
// 用于创建 staging buffer 和 device-local buffer。
// --------------------------------------------------------------------
VkDeviceSize Mesh::vertexDataSize() const {
    return static_cast<VkDeviceSize>(vertices_.size() * sizeof(MeshVertex));
}

// --------------------------------------------------------------------
// Vulkan 顶点绑定描述
//
// binding 0 表示"所有顶点来自同一个 VkBuffer";
// stride 是一个顶点跨越多少字节;
// VERTEX 表示每个顶点前进一次, 而不是每个实例前进一次。
// --------------------------------------------------------------------
VkVertexInputBindingDescription Mesh::bindingDescription() {
    VkVertexInputBindingDescription description{};

    description.binding = 0;
    description.stride = sizeof(MeshVertex);
    description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    return description;
}

// --------------------------------------------------------------------
// Vulkan 顶点属性描述
//
// 每个属性告诉管线:
//   * 从 binding 0 读取;
//   * 对应 shader 的哪个 location;
//   * 数据类型和分量数;
//   * 在顶点结构体中的字节偏移。
//
// 这里用 offsetof 而不是手写数字,
// 以后调整 MeshVertex 成员顺序时不需要同步修改偏移。
// --------------------------------------------------------------------
std::array<VkVertexInputAttributeDescription, 3>
Mesh::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> descriptions{};

    // location 0: vec3 position
    descriptions[0].binding = 0;
    descriptions[0].location = 0;
    descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    descriptions[0].offset = offsetof(MeshVertex, position);

    // location 1: vec3 color
    descriptions[1].binding = 0;
    descriptions[1].location = 1;
    descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    descriptions[1].offset = offsetof(MeshVertex, color);

    // location 2: vec2 texCoord
    descriptions[2].binding = 0;
    descriptions[2].location = 2;
    descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    descriptions[2].offset = offsetof(MeshVertex, texCoord);

    return descriptions;
}
