#include "signals.hpp"

#include <cstdlib>
#include <cstring>

namespace sixo {
namespace {

int name_to_signo(const char* name) {
  if (std::strcmp(name, "SEGV") == 0 || std::strcmp(name, "SIGSEGV") == 0) {
    return SIGSEGV;
  }
  if (std::strcmp(name, "BUS") == 0 || std::strcmp(name, "SIGBUS") == 0) {
    return SIGBUS;
  }
  if (std::strcmp(name, "ILL") == 0 || std::strcmp(name, "SIGILL") == 0) {
    return SIGILL;
  }
  if (std::strcmp(name, "FPE") == 0 || std::strcmp(name, "SIGFPE") == 0) {
    return SIGFPE;
  }
  if (std::strcmp(name, "ABRT") == 0 || std::strcmp(name, "SIGABRT") == 0) {
    return SIGABRT;
  }
  return -1;
}

}  // namespace

int parse_signals(const char* list, int* out, int max_out) {
  if (list == nullptr || out == nullptr || max_out <= 0) {
    return 0;
  }

  int count = 0;
  const char* p = list;
  while (*p != '\0' && count < max_out) {
    while (*p == ' ' || *p == ',') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    char buf[32];
    std::size_t n = 0;
    while (*p != '\0' && *p != ',' && *p != ' ' && n + 1 < sizeof(buf)) {
      buf[n++] = *p++;
    }
    buf[n] = '\0';

    int sig = name_to_signo(buf);
    if (sig < 0) {
      char* end = nullptr;
      long v = std::strtol(buf, &end, 10);
      if (end != buf && *end == '\0' && v > 0 && v < NSIG) {
        sig = static_cast<int>(v);
      }
    }
    if (sig > 0) {
      bool dup = false;
      for (int i = 0; i < count; ++i) {
        if (out[i] == sig) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        out[count++] = sig;
      }
    }
  }
  return count;
}

const char* signal_name(int signo) {
  switch (signo) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGBUS:
      return "SIGBUS";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    case SIGABRT:
      return "SIGABRT";
    default:
      return "SIGNAL";
  }
}

}  // namespace sixo
