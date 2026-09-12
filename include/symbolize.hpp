#pragma once

#include "protocol.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

namespace sixo {

struct MappedFrame {
  std::uint64_t pc = 0;
  std::string file;
  std::uint64_t offset = 0;
  bool mapped = false;
};

struct MapEntry {
  std::uint64_t start = 0;
  std::uint64_t end = 0;
  std::uint64_t offset = 0;
  std::string path;
  bool executable = false;
};

std::vector<MapEntry> parse_maps(pid_t pid);

std::vector<MappedFrame> map_frames(const std::vector<MapEntry>& maps,
                                    const std::uint64_t* pcs,
                                    std::uint32_t count);

// Symbolize frames; prefers llvm-symbolizer, falls back to addr2line.
// Warnings for missing tools go to warning_out (may be nullptr).
std::string symbolize_frames(const std::vector<MappedFrame>& frames,
                             FILE* warning_out);

}  // namespace sixo
