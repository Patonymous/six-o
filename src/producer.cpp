#include "protocol.hpp"
#include "signals.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>

namespace {

using cxa_throw_fn = void (*)(void*, void*, void (*)(void*));

std::atomic<cxa_throw_fn> real_cxa_throw{nullptr};
std::atomic<int> pipe_fd{-1};
volatile sig_atomic_t in_report = 0;

alignas(16) unsigned char altstack_mem[256 * 1024];

cxa_throw_fn resolve_cxa_throw() {
  return reinterpret_cast<cxa_throw_fn>(dlsym(RTLD_NEXT, "__cxa_throw"));
}

void write_all(int fd, const void* data, std::size_t len) {
  const char* p = static_cast<const char*>(data);
  while (len > 0) {
    ssize_t n = ::write(fd, p, len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    p += static_cast<std::size_t>(n);
    len -= static_cast<std::size_t>(n);
  }
}

void emit_event(sixo::Reason reason, int signo) {
  const int fd = pipe_fd.load(std::memory_order_acquire);
  if (fd < 0) {
    return;
  }

  void* frames[sixo::kMaxFrames + 4];
  int captured = ::backtrace(frames, static_cast<int>(sixo::kMaxFrames + 4));
  if (captured < 0) {
    captured = 0;
  }

  // Skip emit_event + caller (hook/handler) + backtrace itself when present.
  constexpr int kSkip = 2;
  int start = captured > kSkip ? kSkip : 0;
  std::uint32_t count =
      static_cast<std::uint32_t>(captured - start);
  if (count > sixo::kMaxFrames) {
    count = sixo::kMaxFrames;
  }

  sixo::Event event{};
  event.header.magic = sixo::kProtocolMagic;
  event.header.version = sixo::kProtocolVersion;
  event.header.reason = static_cast<std::uint32_t>(reason);
  event.header.signo = signo;
  event.header.frame_count = count;
  for (std::uint32_t i = 0; i < count; ++i) {
    event.pcs[i] = reinterpret_cast<std::uint64_t>(frames[start + i]);
  }

  const std::size_t bytes =
      sizeof(sixo::EventHeader) + count * sizeof(std::uint64_t);
  write_all(fd, &event, bytes);
}

void report_signal(int signo) {
  if (in_report) {
    return;
  }
  in_report = 1;
  emit_event(sixo::Reason::Signal, signo);

  struct sigaction sa {};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(signo, &sa, nullptr);
  raise(signo);
}

void signal_handler(int signo, siginfo_t* /*info*/, void* /*context*/) {
  report_signal(signo);
}

void install_signals() {
  const char* list = std::getenv(sixo::kEnvSignals);
  if (list == nullptr || list[0] == '\0') {
    list = sixo::kDefaultSignals;
  }

  int sigs[16];
  const int n = sixo::parse_signals(list, sigs, 16);
  if (n <= 0) {
    return;
  }

  stack_t ss {};
  ss.ss_sp = altstack_mem;
  ss.ss_size = sizeof(altstack_mem);
  ss.ss_flags = 0;
  sigaltstack(&ss, nullptr);

  struct sigaction sa {};
  sa.sa_sigaction = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  for (int i = 0; i < n; ++i) {
    sigaction(sigs[i], &sa, nullptr);
  }
}

void init_pipe_fd() {
  const char* raw = std::getenv(sixo::kEnvPipeFd);
  if (raw == nullptr || raw[0] == '\0') {
    return;
  }
  char* end = nullptr;
  long fd = std::strtol(raw, &end, 10);
  if (end == raw || *end != '\0' || fd < 0) {
    return;
  }
  pipe_fd.store(static_cast<int>(fd), std::memory_order_release);
}

}  // namespace

extern "C" void __cxa_throw(void* thrown_exception, void* tinfo,
                            void (*dest)(void*)) {
  if (!in_report) {
    in_report = 1;
    emit_event(sixo::Reason::Throw, 0);
    in_report = 0;
  }

  cxa_throw_fn fn = real_cxa_throw.load(std::memory_order_acquire);
  if (fn == nullptr) {
    fn = resolve_cxa_throw();
    real_cxa_throw.store(fn, std::memory_order_release);
  }

  if (fn != nullptr) {
    fn(thrown_exception, tinfo, dest);
  }

  std::_Exit(1);
}

__attribute__((constructor)) void sixo_producer_init() {
  init_pipe_fd();
  if (pipe_fd.load(std::memory_order_acquire) >= 0) {
    install_signals();
  }
}
