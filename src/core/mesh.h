// ====================================================================
// mesh.h —— 网格数据 (CPU 端)
//
// Mesh 的职责非常单一:
//   1. 把模型文件里的顶点/索引读进内存;
//   2. 提供连续的内存块, 方便上传到 VkBuffer;
//   3. 告诉渲染管线"顶点在内存里怎么排布"。
//
// Mesh 不持有任何 Vulkan 对象 (VkBuffer/VkDeviceMemory),
// 因此它可以在没有 Vulkan 设备的情况下单独加载、复制和测试。
// GPU 端的缓冲由 ViewportRenderer 的 meshCache 负责创建和缓存。
// ====================================================================
#pragma once

// Vulkan 头文件会间接包含 <windows.h>。
// Windows 头默认定义 min/max 宏, 会和 std::min/std::max 冲突,
// 所以在包含 Vulkan 之前先禁用这两个宏。
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * 一个网格顶点
 *
 * 成员顺序必须与 Mesh::attributeDescriptions() 中的 offset 一致。
 * 当前布局 (共 32 字节):
 *
 *   偏移 0  : position  (vec3, 12 字节) -> location 0
 *   偏移 12 : color     (vec3, 12 字节) -> location 1
 *   偏移 24 : texCoord  (vec2,  8 字节) -> location 2
 */
struct MeshVertex {
    // 顶点位置, 局部坐标
    glm::vec3 position;

    // 顶点颜色; 当前着色器主要使用 Object 的颜色,
    // 这个字段保留给以后做顶点色/材质混合。
    glm::vec3 color;

    // 纹理坐标; 当前没有采样纹理, 但管线布局中保留,
    // 以后接入贴图时不需要再改 Mesh 结构。
    glm::vec2 texCoord;
};

/**
 * 网格
 *
 * 一个 Mesh 通常对应 resources/models 下的一个 .mesh 文件。
 * Object 以值的方式持有一个 Mesh, 因此每个对象都可以有独立模型。
 */
class Mesh {
public:
    // 默认构造: 空网格。Object 的默认参数/容器可能需要它。
    Mesh() = default;

    /**
     * 直接使用内存中的顶点/索引构造
     * @param name     网格名 (通常取文件名)
     * @param vertices 顶点数组
     * @param indices  索引数组 (每 3 个组成一个三角形)
     */
    Mesh(std::string name,
         std::vector<MeshVertex> vertices,
         std::vector<uint32_t> indices);

    /**
     * 从 .mesh 文本文件加载
     *
     * 文件格式:
     *   # 注释
     *   v x y z r g b u v    每个顶点一行
     *   f i0 i1 i2           每个三角形一行
     *
     * @param path 文件路径
     * @return 加载好的 Mesh
     * @throw std::runtime_error 文件打不开、格式错误或索引越界
     */
    static Mesh loadFromFile(const std::string& path);

    /**
     * 从标准 Wavefront .obj 文件加载
     *
     * 支持:
     *   v  : 顶点位置
     *   vt : 纹理坐标 (可选)
     *   f  : 三角形/多边形面 (自动扇形三角化)
     *   v/vt、v//vn、v/vt/vn 形式的索引
     *
     * 法线和材质暂不导入; 顶点颜色统一为白色,
     * 最终颜色由 Object 的颜色决定。
     */
    static Mesh loadFromObjFile(const std::string& path);

    /**
     * 从资源目录加载模型
     *
     * 会依次尝试以下路径:
     *   resources/models/<fileName>
     *   ../resources/models/<fileName>
     *   ../../resources/models/<fileName>
     *   ../../../resources/models/<fileName>
     *   <fileName>
     *
     * 这样无论从项目根目录、build 目录还是 build/shaders 目录启动,
     * 都能找到同一个资源。
     *
     * @param fileName 文件名, 例如 "cube.mesh"
     */
    static Mesh loadFromResource(const std::string& fileName);

    // 网格名, 例如 "cube"
    const std::string& name() const;

    // 只读顶点数组; 渲染器上传时使用
    const std::vector<MeshVertex>& vertices() const;

    // 只读索引数组; 渲染器上传时使用
    const std::vector<uint32_t>& indices() const;

    // 是否没有可绘制的数据
    bool empty() const;

    // 索引数量, 也就是 vkCmdDrawIndexed 的 indexCount
    uint32_t indexCount() const;

    // 连续顶点内存起始地址
    // 因为 std::vector<MeshVertex> 是连续的, 可以直接 memcpy 到 VkBuffer。
    const void* vertexData() const;

    // 连续顶点内存的字节数
    VkDeviceSize vertexDataSize() const;

    // Vulkan 顶点输入绑定描述: 一个 binding, stride=sizeof(MeshVertex)
    static VkVertexInputBindingDescription bindingDescription();

    // Vulkan 顶点属性描述: position/color/texCoord 三个 location
    static std::array<VkVertexInputAttributeDescription, 3>
    attributeDescriptions();

private:
    // 网格名 (来自文件名, 仅用于显示/调试)
    std::string name_;

    // CPU 端顶点数据
    std::vector<MeshVertex> vertices_;

    // CPU 端索引数据, 每个三角形 3 个索引
    std::vector<uint32_t> indices_;
};
