#include "sixo.hpp"

#include <atomic>
#include <cstdlib>
#include <dlfcn.h>

namespace {

using cxa_throw_fn = void (*)(void*, void*, void (*)(void*));
using abort_fn = void (*)();

std::atomic<cxa_throw_fn> real_cxa_throw{nullptr};
std::atomic<abort_fn> real_abort{nullptr};

thread_local bool in_hook = false;

struct ReentrancyGuard {
  bool entered = false;

  ReentrancyGuard() {
    if (!in_hook) {
      in_hook = true;
      entered = true;
    }
  }

  ~ReentrancyGuard() {
    if (entered) {
      in_hook = false;
    }
  }

  ReentrancyGuard(const ReentrancyGuard&) = delete;
  ReentrancyGuard& operator=(const ReentrancyGuard&) = delete;
};

template <typename Fn>
Fn resolve(const char* name) {
  return reinterpret_cast<Fn>(dlsym(RTLD_NEXT, name));
}

}  // namespace

extern "C" void __cxa_throw(void* thrown_exception, void* tinfo,
                            void (*dest)(void*)) {
  {
    ReentrancyGuard guard;
    if (guard.entered) {
      sixo::dump_stacktrace("__cxa_throw");
    }
  }

  cxa_throw_fn fn = real_cxa_throw.load(std::memory_order_acquire);
  if (fn == nullptr) {
    fn = resolve<cxa_throw_fn>("__cxa_throw");
    real_cxa_throw.store(fn, std::memory_order_release);
  }

  if (fn != nullptr) {
    fn(thrown_exception, tinfo, dest);
  }

  std::_Exit(1);
}

extern "C" void abort() {
  {
    ReentrancyGuard guard;
    if (guard.entered) {
      sixo::dump_stacktrace("abort");
    }
  }

  abort_fn fn = real_abort.load(std::memory_order_acquire);
  if (fn == nullptr) {
    fn = resolve<abort_fn>("abort");
    real_abort.store(fn, std::memory_order_release);
  }

  if (fn != nullptr) {
    fn();
  }

  std::_Exit(134);
}
