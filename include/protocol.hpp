#pragma once

#include <cstdint>

namespace sixo {

inline constexpr std::uint32_t kProtocolMagic = 0x30584953u;  // 'SIX0' LE
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kMaxFrames = 64;

enum class Reason : std::uint32_t {
  Throw = 1,
  Signal = 2,
};

// Wire layout: fixed header + up to kMaxFrames PCs. Writers may send a short
// record (header + frame_count * 8) in a single write(2).
struct EventHeader {
  std::uint32_t magic = kProtocolMagic;
  std::uint32_t version = kProtocolVersion;
  std::uint32_t reason = 0;
  std::int32_t signo = 0;
  std::uint32_t frame_count = 0;
  std::uint32_t reserved = 0;  // keep header 8-byte aligned / 24 bytes
};

struct Event {
  EventHeader header{};
  std::uint64_t pcs[kMaxFrames]{};
};

inline constexpr char kDefaultSignals[] = "SEGV,BUS,ILL,FPE,ABRT";

inline constexpr char kEnvPipeFd[] = "SIXO_PIPE_FD";
inline constexpr char kEnvSignals[] = "SIXO_SIGNALS";
inline constexpr char kEnvOutput[] = "SIXO_OUTPUT";
inline constexpr char kEnvPreload[] = "SIXO_PRELOAD";

}  // namespace sixo
