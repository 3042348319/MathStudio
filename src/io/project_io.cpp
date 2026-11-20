// ====================================================================
// project_io.cpp —— .math 工程保存/加载实现
// ====================================================================

#include "project_io.h"

#include "compression.h"
#include "byte_stream.h"
#include "menu.h"
#include "object.h"
#include "plugin.h"
#include "plugin_factory.h"
#include "sub_window.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace {

constexpr std::uint32_t kMaxWindows = 64;
constexpr std::uint32_t kMaxObjects = 100000;
constexpr std::uint32_t kMaxVertices = 5000000;
constexpr std::uint32_t kMaxIndices = 15000000;

void writeMesh(BinaryWriter& out, const Mesh& mesh) {
    out.writeString(mesh.name());

    out.writeU32(static_cast<std::uint32_t>(mesh.vertices().size()));
    for (const MeshVertex& vertex : mesh.vertices()) {
        out.writeVec3(vertex.position);
        out.writeVec3(vertex.color);
        out.writeVec2(vertex.texCoord);
    }

    out.writeU32(static_cast<std::uint32_t>(mesh.indices().size()));
    for (std::uint32_t index : mesh.indices()) {
        out.writeU32(index);
    }
}

bool readMesh(BinaryReader& in, Mesh& mesh) {
    const std::string name = in.readString();
    const std::uint32_t vertexCount = in.readU32();
    if (!in.ok() || vertexCount > kMaxVertices) {
        return false;
    }

    std::vector<MeshVertex> vertices;
    vertices.reserve(vertexCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i) {
        MeshVertex vertex{};
        vertex.position = in.readVec3();
        vertex.color = in.readVec3();
        vertex.texCoord = in.readVec2();
        vertices.push_back(vertex);
    }

    const std::uint32_t indexCount = in.readU32();
    if (!in.ok() || indexCount > kMaxIndices) {
        return false;
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(indexCount);
    for (std::uint32_t i = 0; i < indexCount; ++i) {
        indices.push_back(in.readU32());
    }

    if (!in.ok()) {
        return false;
    }

    mesh = Mesh(name, std::move(vertices), std::move(indices));
    return true;
}

void writeSubWindow(BinaryWriter& out, const SubWindow& window,
                    bool writeVisibility) {
    out.writeString(window.title());

    const ViewportCamera camera = window.camera();
    out.writeVec3(camera.target);
    out.writeFloat(camera.yaw);
    out.writeFloat(camera.pitch);
    out.writeFloat(camera.distance);
    out.writeFloat(camera.fov);
    out.writeFloat(camera.nearPlane);
    out.writeFloat(camera.farPlane);

    const auto& objects = window.objects();
    out.writeU32(static_cast<std::uint32_t>(objects.size()));
    for (const auto& object : objects) {
        out.writeString(object->name());
        out.writeVec3(object->position());
        out.writeVec4(object->color());
        if (writeVisibility) {
            out.writeBool(object->visible());
        }
        writeMesh(out, object->mesh());

        out.writeU32(
            static_cast<std::uint32_t>(object->plugins().size()));
        for (const auto& plugin : object->plugins()) {
            out.writeString(plugin->typeId());
            plugin->save(out);
        }
    }

    // 组别只保存对象索引, 加载时对象 ID 会重新生成。
    const auto& groups = window.hierarchy().groups();
    out.writeU32(static_cast<std::uint32_t>(groups.size()));
    for (const ObjectGroup& group : groups) {
        out.writeString(group.name);
        out.writeU32(
            static_cast<std::uint32_t>(group.objects.size()));
        for (ObjectId id : group.objects) {
            const std::size_t index = window.indexOfObject(id);
            out.writeU32(static_cast<std::uint32_t>(
                index < objects.size() ? index : 0));
        }
    }

    const auto& ungrouped = window.hierarchy().ungrouped();
    out.writeU32(static_cast<std::uint32_t>(ungrouped.size()));
    for (ObjectId id : ungrouped) {
        const std::size_t index = window.indexOfObject(id);
        out.writeU32(static_cast<std::uint32_t>(
            index < objects.size() ? index : 0));
    }
}

bool readSubWindow(BinaryReader& in, Menu& menu,
                   bool readVisibility) {
    const std::string title = in.readString();
    if (!in.ok()) {
        return false;
    }

    auto windowPtr = std::make_unique<SubWindow>(
        menu.viewportContext(), title, 640, 480);
    SubWindow& window = menu.addSubWindow(std::move(windowPtr));
    window.clearObjects();

    ViewportCamera camera;
    camera.target = in.readVec3();
    camera.yaw = in.readFloat();
    camera.pitch = in.readFloat();
    camera.distance = in.readFloat();
    camera.fov = in.readFloat();
    camera.nearPlane = in.readFloat();
    camera.farPlane = in.readFloat();
    window.setCamera(camera);

    const std::uint32_t objectCount = in.readU32();
    if (!in.ok() || objectCount > kMaxObjects) {
        return false;
    }

    for (std::uint32_t i = 0; i < objectCount; ++i) {
        const std::string name = in.readString();
        const glm::vec3 position = in.readVec3();
        const glm::vec4 color = in.readVec4();
        const bool visible = readVisibility ? in.readBool() : true;

        Mesh mesh;
        if (!readMesh(in, mesh)) {
            return false;
        }

        Object& object = window.addObject<Object>(
            std::move(mesh), position, color, name);
        object.setVisible(visible);

        const std::uint32_t pluginCount = in.readU32();
        if (!in.ok() || pluginCount > 1024) {
            return false;
        }

        for (std::uint32_t p = 0; p < pluginCount; ++p) {
            const std::string type = in.readString();
            std::unique_ptr<Plugin> plugin = createPluginByType(type);
            if (!plugin) {
                return false;
            }

            Plugin& added = object.addPlugin(std::move(plugin));
            if (!added.load(in)) {
                return false;
            }
        }
    }

    const std::uint32_t groupCount = in.readU32();
    if (!in.ok() || groupCount > 10000) {
        return false;
    }

    for (std::uint32_t g = 0; g < groupCount; ++g) {
        const std::string groupName = in.readString();
        const std::uint64_t groupId =
            window.hierarchy().createGroup(groupName);

        const std::uint32_t memberCount = in.readU32();
        if (!in.ok() || memberCount > objectCount) {
            return false;
        }
        for (std::uint32_t m = 0; m < memberCount; ++m) {
            const std::uint32_t index = in.readU32();
            if (index < objectCount) {
                window.hierarchy().moveObject(
                    window.objects()[index]->id(), groupId);
            }
        }
    }

    const std::uint32_t ungroupedCount = in.readU32();
    if (!in.ok() || ungroupedCount > objectCount) {
        return false;
    }
    for (std::uint32_t i = 0; i < ungroupedCount; ++i) {
        const std::uint32_t index = in.readU32();
        if (index < objectCount) {
            window.hierarchy().moveObject(
                window.objects()[index]->id(), std::nullopt);
        }
    }

    return in.ok();
}

}  // namespace

bool saveMathProject(const Menu& menu,
                     const std::string& path,
                     std::string& error) {
    // 格式版本: 旧版文件没有 magic, 仍按旧格式读取。
    constexpr std::uint32_t kFormatMagic = 0x4D53544Du;  // "MSTM"
    constexpr std::uint32_t kFormatVersion = 2;

    BinaryWriter out;
    out.writeString(menu.projectName());
    out.writeU32(kFormatMagic);
    out.writeU32(kFormatVersion);

    const auto& windows = menu.subWindows();
    out.writeU32(static_cast<std::uint32_t>(windows.size()));
    for (const auto& window : windows) {
        writeSubWindow(out, *window, true);
    }

    std::error_code filesystemError;
    const std::filesystem::path parent =
        std::filesystem::u8path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystemError);
    }

    return writeMathFile(path, out.data(), error);
}

bool loadMathProject(Menu& menu,
                     const std::string& path,
                     std::string& error) {
    std::vector<std::uint8_t> raw;
    if (!readMathFile(path, raw, error)) {
        return false;
    }

    BinaryReader in(raw);
    const std::string projectName = in.readString();
    std::uint32_t windowCount = in.readU32();
    bool readVisibility = false;

    constexpr std::uint32_t kFormatMagic = 0x4D53544Du;  // "MSTM"
    if (windowCount == kFormatMagic) {
        const std::uint32_t version = in.readU32();
        windowCount = in.readU32();
        readVisibility = version >= 2;
    }

    if (!in.ok() || windowCount > kMaxWindows) {
        error = "工程文件中的子窗口数量非法";
        return false;
    }

    menu.clearSubWindows();
    menu.setProjectName(projectName.empty() ? "未命名工程"
                                            : projectName);
    menu.setProjectPath(path);

    for (std::uint32_t i = 0; i < windowCount; ++i) {
        if (!readSubWindow(in, menu, readVisibility)) {
            error = "工程文件中的子窗口数据损坏";
            return false;
        }
    }

    if (!in.ok()) {
        error = "工程文件数据不完整";
        return false;
    }

    menu.openSubWindow(0);
    return true;
}
