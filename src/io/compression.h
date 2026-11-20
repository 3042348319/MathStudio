// ====================================================================
// compression.h —— .math 工程文件的轻量压缩容器
//
// 文件结构:
//   "MATH" | version(u32) | codec(u8) | reserved(3) |
//   rawSize(u64) | payloadSize(u64) | payload
//
// codec:
//   0 = 原始数据
//   1 = RLE (适合工程中的大量 0 字节/重复结构)
// ====================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

bool writeMathFile(const std::string& path,
                   const std::vector<std::uint8_t>& raw,
                   std::string& error);

bool readMathFile(const std::string& path,
                  std::vector<std::uint8_t>& raw,
                  std::string& error);
