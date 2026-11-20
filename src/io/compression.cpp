// ====================================================================
// compression.cpp —— RLE 压缩 + .math 容器
// ====================================================================

#include "compression.h"

#include "byte_stream.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

constexpr char kMagic[4] = {'M', 'A', 'T', 'H'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint8_t kCodecRaw = 0;
constexpr std::uint8_t kCodecRle = 1;

/**
 * RLE 格式:
 *   普通字节   -> 原样输出
 *   0x00 0x00  -> 一个字面量 0
 *   0x00 count(u16) value -> count 个 value (count >= 4)
 */
std::vector<std::uint8_t> rleCompress(
    const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> output;
    output.reserve(input.size());

    std::size_t i = 0;
    while (i < input.size()) {
        const std::uint8_t value = input[i];
        std::size_t run = 1;
        while (i + run < input.size() &&
               input[i + run] == value &&
               run < std::numeric_limits<std::uint16_t>::max()) {
            ++run;
        }

        if (run >= 4) {
            output.push_back(0x00);
            output.push_back(
                static_cast<std::uint8_t>(run & 0xffu));
            output.push_back(
                static_cast<std::uint8_t>((run >> 8) & 0xffu));
            output.push_back(value);
            i += run;
        } else {
            if (value == 0x00) {
                output.push_back(0x00);
                output.push_back(0x00);
                output.push_back(0x00);
            } else {
                output.push_back(value);
            }
            ++i;
        }
    }

    return output;
}

bool rleDecompress(const std::vector<std::uint8_t>& input,
                   std::vector<std::uint8_t>& output,
                   std::size_t expectedSize,
                   std::string& error) {
    output.clear();
    output.reserve(expectedSize);

    std::size_t i = 0;
    while (i < input.size()) {
        const std::uint8_t value = input[i++];
        if (value != 0x00) {
            output.push_back(value);
            continue;
        }

        if (i + 2 > input.size()) {
            error = "压缩数据结尾不完整";
            return false;
        }

        const std::uint16_t count =
            static_cast<std::uint16_t>(input[i]) |
            static_cast<std::uint16_t>(input[i + 1]) << 8;
        i += 2;

        if (count == 0) {
            output.push_back(0x00);
            continue;
        }

        if (count < 4 || i >= input.size()) {
            error = "压缩数据中的游程长度非法";
            return false;
        }

        const std::uint8_t repeated = input[i++];
        output.insert(output.end(), count, repeated);

        if (output.size() > expectedSize) {
            error = "解压后数据超过头文件声明的大小";
            return false;
        }
    }

    if (output.size() != expectedSize) {
        error = "解压后数据大小与头文件不一致";
        return false;
    }
    return true;
}

}  // namespace

bool writeMathFile(const std::string& path,
                   const std::vector<std::uint8_t>& raw,
                   std::string& error) {
    std::vector<std::uint8_t> compressed = rleCompress(raw);

    std::uint8_t codec = kCodecRle;
    const std::vector<std::uint8_t>* payload = &compressed;
    if (compressed.size() >= raw.size()) {
        codec = kCodecRaw;
        payload = &raw;
    }

    BinaryWriter header;
    header.writePod(kMagic);
    header.writeU32(kVersion);
    header.writeU8(codec);
    header.writeU8(0);
    header.writeU8(0);
    header.writeU8(0);
    header.writeU64(static_cast<std::uint64_t>(raw.size()));
    header.writeU64(static_cast<std::uint64_t>(payload->size()));

    std::ofstream file(std::filesystem::u8path(path),
                       std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "无法写入工程文件: " + path;
        return false;
    }

    file.write(reinterpret_cast<const char*>(header.data().data()),
               static_cast<std::streamsize>(header.data().size()));
    file.write(reinterpret_cast<const char*>(payload->data()),
               static_cast<std::streamsize>(payload->size()));
    if (!file.good()) {
        error = "写工程文件失败: " + path;
        return false;
    }
    return true;
}

bool readMathFile(const std::string& path,
                  std::vector<std::uint8_t>& raw,
                  std::string& error) {
    std::ifstream file(std::filesystem::u8path(path),
                       std::ios::binary);
    if (!file.is_open()) {
        error = "无法打开工程文件: " + path;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fileSize < 0) {
        error = "无法读取工程文件大小";
        return false;
    }

    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(fileSize));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
        if (!file.good()) {
            error = "读取工程文件失败";
            return false;
        }
    }

    BinaryReader reader(bytes);
    char magic[4] = {};
    reader.readPod(magic);
    if (!reader.ok() || std::memcmp(magic, kMagic, 4) != 0) {
        error = "不是有效的 .math 工程文件";
        return false;
    }

    const std::uint32_t version = reader.readU32();
    if (!reader.ok() || version != kVersion) {
        error = "工程文件版本不支持";
        return false;
    }

    const std::uint8_t codec = reader.readU8();
    reader.readU8();
    reader.readU8();
    reader.readU8();
    const std::uint64_t rawSize = reader.readU64();
    const std::uint64_t payloadSize = reader.readU64();
    if (!reader.ok() ||
        rawSize > 512ull * 1024ull * 1024ull ||
        payloadSize > fileSize) {
        error = "工程文件头损坏";
        return false;
    }

    if (reader.remaining() < payloadSize) {
        error = "工程文件数据不完整";
        return false;
    }

    const auto* payload = bytes.data() + (bytes.size() - reader.remaining());
    std::vector<std::uint8_t> compressed(
        payload, payload + static_cast<std::size_t>(payloadSize));

    if (codec == kCodecRaw) {
        if (compressed.size() != rawSize) {
            error = "原始数据大小与头文件不一致";
            return false;
        }
        raw = std::move(compressed);
        return true;
    }

    if (codec != kCodecRle) {
        error = "未知的工程压缩格式";
        return false;
    }

    return rleDecompress(compressed, raw,
                         static_cast<std::size_t>(rawSize), error);
}
