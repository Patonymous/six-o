#include "symbolize.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace sixo {
namespace {

bool command_exists(const char* name) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return false;
  }
  std::string path = path_env;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find(':', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    std::string dir = path.substr(start, end - start);
    if (!dir.empty()) {
      std::string candidate = dir + "/" + name;
      if (access(candidate.c_str(), X_OK) == 0) {
        return true;
      }
    }
    if (end == path.size()) {
      break;
    }
    start = end + 1;
  }
  return false;
}

std::string run_capture(const std::string& cmd) {
  FILE* pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }
  std::string out;
  char buf[512];
  while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
    out += buf;
  }
  pclose(pipe);
  return out;
}

std::string shell_escape(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::string format_raw(const std::vector<MappedFrame>& frames) {
  std::ostringstream out;
  for (const auto& f : frames) {
    out << "  0x" << std::hex << f.pc << std::dec;
    if (f.mapped && !f.file.empty()) {
      out << " " << f.file << "+0x" << std::hex << f.offset << std::dec;
    }
    out << "\n";
  }
  return out.str();
}

std::string symbolize_llvm(const std::vector<MappedFrame>& frames) {
  std::ostringstream input;
  std::vector<bool> mapped_flags;
  mapped_flags.reserve(frames.size());
  for (const auto& f : frames) {
    const bool ok = f.mapped && !f.file.empty();
    mapped_flags.push_back(ok);
    if (ok) {
      input << f.file << " 0x" << std::hex << f.offset << std::dec << "\n";
    }
  }

  if (input.str().empty()) {
    return format_raw(frames);
  }

  char tmpl[] = "/tmp/sixo-sym-XXXXXX";
  int tfd = mkstemp(tmpl);
  if (tfd < 0) {
    return format_raw(frames);
  }
  const std::string payload = input.str();
  if (write(tfd, payload.data(), payload.size()) < 0) {
    close(tfd);
    unlink(tmpl);
    return format_raw(frames);
  }
  close(tfd);

  std::ostringstream cmd;
  cmd << "llvm-symbolizer --demangle < " << shell_escape(tmpl)
      << " 2>/dev/null";
  std::string raw = run_capture(cmd.str());
  unlink(tmpl);

  // llvm-symbolizer emits blank-line-separated records per input address.
  std::vector<std::string> records;
  {
    std::string cur;
    std::istringstream in(raw);
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) {
        if (!cur.empty()) {
          records.push_back(cur);
          cur.clear();
        }
      } else {
        if (!cur.empty()) {
          cur.push_back('\n');
        }
        cur += line;
      }
    }
    if (!cur.empty()) {
      records.push_back(cur);
    }
  }

  std::ostringstream out;
  std::size_t ri = 0;
  for (std::size_t i = 0; i < frames.size(); ++i) {
    if (!mapped_flags[i]) {
      out << "  0x" << std::hex << frames[i].pc << std::dec << "\n";
      continue;
    }
    if (ri < records.size()) {
      out << records[ri++] << "\n";
    } else {
      out << "  0x" << std::hex << frames[i].pc << std::dec << "\n";
    }
  }
  return out.str();
}

std::string symbolize_addr2line(const std::vector<MappedFrame>& frames) {
  std::ostringstream out;
  for (const auto& f : frames) {
    if (!f.mapped || f.file.empty()) {
      out << "??:0\n";
      continue;
    }
    std::ostringstream cmd;
    cmd << "addr2line -Cfpe " << shell_escape(f.file) << " 0x" << std::hex
        << f.offset << " 2>/dev/null";
    std::string line = run_capture(cmd.str());
    if (line.empty()) {
      out << "0x" << std::hex << f.pc << std::dec << "\n";
    } else {
      out << line;
      if (line.back() != '\n') {
        out << '\n';
      }
    }
  }
  return out.str();
}

}  // namespace

std::vector<MapEntry> parse_maps(pid_t pid) {
  std::vector<MapEntry> maps;
  std::string path = "/proc/" + std::to_string(pid) + "/maps";
  std::ifstream in(path);
  if (!in) {
    return maps;
  }

  std::string line;
  while (std::getline(in, line)) {
    // start-end perms offset dev inode pathname
    MapEntry e;
    char perms[8] = {};
    unsigned long long start = 0;
    unsigned long long end = 0;
    unsigned long long offset = 0;
    char pathbuf[4096] = {};
    // pathname may be empty
    int n = std::sscanf(line.c_str(), "%llx-%llx %7s %llx %*s %*s %4095[^\n]",
                        &start, &end, perms, &offset, pathbuf);
    if (n < 4) {
      continue;
    }
    e.start = start;
    e.end = end;
    e.offset = offset;
    e.executable = std::strchr(perms, 'x') != nullptr;
    if (n >= 5) {
      // trim leading spaces from pathname
      const char* p = pathbuf;
      while (*p == ' ') {
        ++p;
      }
      e.path = p;
    }
    if (e.executable && !e.path.empty() && e.path[0] != '[') {
      maps.push_back(std::move(e));
    }
  }
  return maps;
}

std::vector<MappedFrame> map_frames(const std::vector<MapEntry>& maps,
                                    const std::uint64_t* pcs,
                                    std::uint32_t count) {
  std::vector<MappedFrame> out;
  out.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    MappedFrame f;
    f.pc = pcs[i];
    for (const auto& m : maps) {
      if (f.pc >= m.start && f.pc < m.end && !m.path.empty() &&
          m.path[0] != '[') {
        f.mapped = true;
        f.file = m.path;
        // File offset for ELF: map_offset + (pc - map_start)
        f.offset = m.offset + (f.pc - m.start);
        break;
      }
    }
    out.push_back(std::move(f));
  }
  return out;
}

std::string symbolize_frames(const std::vector<MappedFrame>& frames,
                             FILE* warning_out) {
  static bool warned_fallback = false;
  static bool warned_none = false;

  if (command_exists("llvm-symbolizer")) {
    std::string text = symbolize_llvm(frames);
    if (!text.empty()) {
      return text;
    }
  }

  if (command_exists("addr2line")) {
    if (warning_out != nullptr && !warned_fallback) {
      std::fputs("six-o: warning: llvm-symbolizer not available; "
                 "falling back to addr2line\n",
                 warning_out);
      warned_fallback = true;
    }
    return symbolize_addr2line(frames);
  }

  if (warning_out != nullptr && !warned_none) {
    std::fputs("six-o: warning: neither llvm-symbolizer nor addr2line found; "
               "printing raw addresses\n",
               warning_out);
    warned_none = true;
  }
  return format_raw(frames);
}

}  // namespace sixo
