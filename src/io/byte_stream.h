// ====================================================================
// byte_stream.h —— .math 工程的二进制读写工具
//
// 职责单一:
//   * 按固定小端格式写入 uint32/uint64/float/字符串/向量;
//   * 读取时检查边界, 失败只置 ok=false, 不抛异常。
//
// 工程文件格式由 project_io.cpp 组合这些基础类型。
// ====================================================================
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

/** 二进制写入器 */
class BinaryWriter {
public:
    template <typename T>
    void writePod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "writePod requires trivially copyable type");
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        data_.insert(data_.end(), bytes, bytes + sizeof(T));
    }

    void writeU8(std::uint8_t value) {
        writePod(value);
    }

    void writeU32(std::uint32_t value) {
        writePod(value);
    }

    void writeU64(std::uint64_t value) {
        writePod(value);
    }

    void writeFloat(float value) {
        writePod(value);
    }

    void writeBool(bool value) {
        writeU8(value ? 1u : 0u);
    }

    void writeString(const std::string& value) {
        const std::uint32_t size =
            static_cast<std::uint32_t>(value.size());
        writeU32(size);
        data_.insert(data_.end(), value.begin(), value.end());
    }

    void writeVec2(const glm::vec2& value) {
        writeFloat(value.x);
        writeFloat(value.y);
    }

    void writeVec3(const glm::vec3& value) {
        writeFloat(value.x);
        writeFloat(value.y);
        writeFloat(value.z);
    }

    void writeVec4(const glm::vec4& value) {
        writeFloat(value.x);
        writeFloat(value.y);
        writeFloat(value.z);
        writeFloat(value.w);
    }

    const std::vector<std::uint8_t>& data() const {
        return data_;
    }

    std::vector<std::uint8_t>& data() {
        return data_;
    }

private:
    std::vector<std::uint8_t> data_;
};

/** 二进制读取器; 任何越界访问都会把 ok 置为 false */
class BinaryReader {
public:
    BinaryReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {
    }

    explicit BinaryReader(const std::vector<std::uint8_t>& data)
        : BinaryReader(data.data(), data.size()) {
    }

    template <typename T>
    bool readPod(T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "readPod requires trivially copyable type");
        if (!ok_ || remaining() < sizeof(T)) {
            ok_ = false;
            return false;
        }
        std::memcpy(&value, data_ + position_, sizeof(T));
        position_ += sizeof(T);
        return true;
    }

    std::uint8_t readU8() {
        std::uint8_t value = 0;
        readPod(value);
        return value;
    }

    std::uint32_t readU32() {
        std::uint32_t value = 0;
        readPod(value);
        return value;
    }

    std::uint64_t readU64() {
        std::uint64_t value = 0;
        readPod(value);
        return value;
    }

    float readFloat() {
        float value = 0.0f;
        readPod(value);
        return value;
    }

    bool readBool() {
        return readU8() != 0;
    }

    std::string readString(std::uint32_t maxLength = 4u * 1024u * 1024u) {
        const std::uint32_t length = readU32();
        if (!ok_ || length > maxLength || remaining() < length) {
            ok_ = false;
            return std::string();
        }

        std::string value(
            reinterpret_cast<const char*>(data_ + position_), length);
        position_ += length;
        return value;
    }

    /**
     * 读取任意原始字节块。
     * 动态插件的状态 blob 使用它, 而不是逐字节读 U32。
     */
    bool readBytes(void* destination, std::size_t count) {
        if (!ok_ || destination == nullptr || remaining() < count) {
            ok_ = false;
            return false;
        }
        std::memcpy(destination, data_ + position_, count);
        position_ += count;
        return true;
    }

    glm::vec2 readVec2() {
        glm::vec2 value{0.0f};
        value.x = readFloat();
        value.y = readFloat();
        return value;
    }

    glm::vec3 readVec3() {
        glm::vec3 value{0.0f};
        value.x = readFloat();
        value.y = readFloat();
        value.z = readFloat();
        return value;
    }

    glm::vec4 readVec4() {
        glm::vec4 value{0.0f};
        value.x = readFloat();
        value.y = readFloat();
        value.z = readFloat();
        value.w = readFloat();
        return value;
    }

    bool ok() const {
        return ok_;
    }

    std::size_t remaining() const {
        return position_ <= size_ ? size_ - position_ : 0;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    bool ok_ = true;
};
