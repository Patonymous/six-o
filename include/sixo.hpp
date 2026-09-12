#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stacktrace>
#include <string>

namespace sixo {

inline FILE* open_output() {
  const char* path = std::getenv("SIXO_OUTPUT");
  if (path == nullptr || path[0] == '\0' || std::strcmp(path, "-") == 0) {
    return stderr;
  }

  FILE* file = std::fopen(path, "a");
  if (file == nullptr) {
    return stderr;
  }
  return file;
}

inline void close_output(FILE* file) {
  if (file != nullptr && file != stderr) {
    std::fclose(file);
  }
}

inline void dump_stacktrace(const char* reason) {
  FILE* out = open_output();
  std::fprintf(out, "six-o: %s\n", reason);

  // Skip this frame and the caller hook frame.
  const auto trace = std::stacktrace::current(/*skip=*/2);
  const std::string text = std::to_string(trace);
  std::fwrite(text.data(), 1, text.size(), out);
  if (text.empty() || text.back() != '\n') {
    std::fputc('\n', out);
  }
  std::fflush(out);
  close_output(out);
}

}  // namespace sixo
