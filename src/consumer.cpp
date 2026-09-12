#include "protocol.hpp"
#include "signals.hpp"
#include "symbolize.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void usage() {
  std::fputs(
      "usage: six-o [--output PATH] [--signals LIST] [--preload PATH] [--] "
      "command [args...]\n"
      "\n"
      "  --output PATH    Write stacktraces to PATH (default: -, stderr).\n"
      "                   Also SIXO_OUTPUT.\n"
      "  --signals LIST   Comma-separated signals to catch\n"
      "                   (default: SEGV,BUS,ILL,FPE,ABRT). Also SIXO_SIGNALS.\n"
      "  --preload PATH   Path to six-o.so (default: beside this binary).\n"
      "                   Also SIXO_PRELOAD.\n",
      stderr);
  std::exit(2);
}

std::string default_preload_path(const char* argv0) {
  std::error_code ec;
  fs::path self = fs::absolute(argv0, ec);
  if (ec) {
    self = argv0;
  }
  return (self.parent_path() / "six-o.so").string();
}

FILE* open_output(const std::string& path) {
  if (path.empty() || path == "-") {
    return stderr;
  }
  FILE* f = std::fopen(path.c_str(), "a");
  if (f == nullptr) {
    std::perror("six-o: fopen");
    return stderr;
  }
  return f;
}

void close_output(FILE* f) {
  if (f != nullptr && f != stderr) {
    std::fclose(f);
  }
}

bool read_full(int fd, void* buf, std::size_t n) {
  char* p = static_cast<char*>(buf);
  std::size_t got = 0;
  while (got < n) {
    ssize_t r = ::read(fd, p + got, n - got);
    if (r == 0) {
      return false;
    }
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    got += static_cast<std::size_t>(r);
  }
  return true;
}

bool read_event(int fd, sixo::Event& event) {
  if (!read_full(fd, &event.header, sizeof(event.header))) {
    return false;
  }
  if (event.header.magic != sixo::kProtocolMagic ||
      event.header.version != sixo::kProtocolVersion) {
    return false;
  }
  if (event.header.frame_count > sixo::kMaxFrames) {
    return false;
  }
  const std::size_t bytes =
      event.header.frame_count * sizeof(std::uint64_t);
  if (bytes == 0) {
    return true;
  }
  return read_full(fd, event.pcs, bytes);
}

void print_event(FILE* out, const std::vector<sixo::MapEntry>& maps,
                 const sixo::Event& event) {
  if (event.header.reason ==
      static_cast<std::uint32_t>(sixo::Reason::Throw)) {
    std::fputs("six-o: __cxa_throw\n", out);
  } else if (event.header.reason ==
             static_cast<std::uint32_t>(sixo::Reason::Signal)) {
    std::fprintf(out, "six-o: %s\n", sixo::signal_name(event.header.signo));
  } else {
    std::fputs("six-o: unknown\n", out);
  }

  auto frames =
      sixo::map_frames(maps, event.pcs, event.header.frame_count);
  std::string text = sixo::symbolize_frames(frames, stderr);
  std::fwrite(text.data(), 1, text.size(), out);
  if (!text.empty() && text.back() != '\n') {
    std::fputc('\n', out);
  }
  std::fflush(out);
}

int exit_status_from_wait(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::string output =
      std::getenv(sixo::kEnvOutput) ? std::getenv(sixo::kEnvOutput) : "-";
  std::string signals = std::getenv(sixo::kEnvSignals)
                            ? std::getenv(sixo::kEnvSignals)
                            : sixo::kDefaultSignals;
  std::string preload = std::getenv(sixo::kEnvPreload)
                            ? std::getenv(sixo::kEnvPreload)
                            : default_preload_path(argv[0]);

  int argi = 1;
  while (argi < argc) {
    const char* a = argv[argi];
    if (std::strcmp(a, "--") == 0) {
      ++argi;
      break;
    }
    if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      usage();
    }
    if (std::strcmp(a, "--output") == 0) {
      if (argi + 1 >= argc) {
        usage();
      }
      output = argv[argi + 1];
      argi += 2;
      continue;
    }
    if (std::strcmp(a, "--signals") == 0) {
      if (argi + 1 >= argc) {
        usage();
      }
      signals = argv[argi + 1];
      argi += 2;
      continue;
    }
    if (std::strcmp(a, "--preload") == 0) {
      if (argi + 1 >= argc) {
        usage();
      }
      preload = argv[argi + 1];
      argi += 2;
      continue;
    }
    if (a[0] == '-') {
      std::fprintf(stderr, "six-o: unknown option: %s\n", a);
      usage();
    }
    break;
  }

  if (argi >= argc) {
    usage();
  }

  if (access(preload.c_str(), R_OK) != 0) {
    std::fprintf(stderr, "six-o: preload library not found: %s\n",
                 preload.c_str());
    return 1;
  }

  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC) != 0) {
    std::perror("six-o: pipe2");
    return 1;
  }

  int flags = fcntl(pipefd[1], F_GETFD);
  if (flags >= 0) {
    fcntl(pipefd[1], F_SETFD, flags & ~FD_CLOEXEC);
  }

  pid_t child = fork();
  if (child < 0) {
    std::perror("six-o: fork");
    return 1;
  }

  if (child == 0) {
    close(pipefd[0]);

    std::string fd_str = std::to_string(pipefd[1]);
    setenv(sixo::kEnvPipeFd, fd_str.c_str(), 1);
    setenv(sixo::kEnvSignals, signals.c_str(), 1);

    std::string ld = preload;
    if (const char* existing = std::getenv("LD_PRELOAD")) {
      ld.push_back(':');
      ld += existing;
    }
    setenv("LD_PRELOAD", ld.c_str(), 1);

    execvp(argv[argi], argv + argi);
    std::perror("six-o: execvp");
    _exit(127);
  }

  close(pipefd[1]);
  FILE* out = open_output(output);

  // Wait until the child has exec'd far enough that its maps include the
  // target binary (avoid symbolizing against pre-exec maps).
  const fs::path target_path = fs::absolute(argv[argi]);
  const std::string target_str = target_path.string();
  const std::string target_name = target_path.filename().string();
  std::vector<sixo::MapEntry> maps_cache;
  for (int i = 0; i < 2000; ++i) {
    maps_cache = sixo::parse_maps(child);
    bool ready = false;
    for (const auto& m : maps_cache) {
      if (m.path == target_str || m.path.ends_with(target_name)) {
        ready = true;
        break;
      }
    }
    if (ready) {
      break;
    }
    int st = 0;
    pid_t r = waitpid(child, &st, WNOHANG);
    if (r == child) {
      // Child already exited; use whatever maps we have (may be empty).
      break;
    }
    usleep(1000);
  }

  bool child_reaped = false;
  int status = 0;

  while (true) {
    if (!child_reaped) {
      auto refreshed = sixo::parse_maps(child);
      if (!refreshed.empty()) {
        maps_cache = std::move(refreshed);
      }
    }

    pollfd pfd{};
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 20);
    if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
      if (pfd.revents & POLLIN) {
        sixo::Event event{};
        if (!read_event(pipefd[0], event)) {
          break;
        }
        if (!child_reaped) {
          auto refreshed = sixo::parse_maps(child);
          if (!refreshed.empty()) {
            maps_cache = std::move(refreshed);
          }
        }
        print_event(out, maps_cache, event);
        continue;
      }
      if (pfd.revents & (POLLHUP | POLLERR)) {
        sixo::Event event{};
        while (read_event(pipefd[0], event)) {
          print_event(out, maps_cache, event);
        }
        break;
      }
    }

    if (!child_reaped) {
      pid_t r = waitpid(child, &status, WNOHANG);
      if (r == child) {
        child_reaped = true;
      }
    } else if (pr == 0) {
      sixo::Event event{};
      while (read_event(pipefd[0], event)) {
        print_event(out, maps_cache, event);
      }
      break;
    }
  }

  close(pipefd[0]);

  if (!child_reaped) {
    while (waitpid(child, &status, 0) < 0) {
      if (errno != EINTR) {
        std::perror("six-o: waitpid");
        close_output(out);
        return 1;
      }
    }
  }

  close_output(out);
  return exit_status_from_wait(status);
}
