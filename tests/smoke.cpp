#include <cstdlib>
#include <cstring>
#include <stdexcept>

[[gnu::noinline]] void sixo_smoke_throw_site() {
  throw std::runtime_error("smoke throw");
}

[[gnu::noinline]] void sixo_smoke_abort_site() {
  // Call via volatile pointer so the compiler keeps a post-call IP inside
  // this function for backtraces (std::abort is noreturn).
  void (*volatile abort_fn)() = std::abort;
  abort_fn();
  volatile int keep = 0;
  (void)keep;
}

[[gnu::noinline]] void sixo_smoke_segfault_site() {
  volatile int* p = nullptr;
  *p = 42;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    return 2;
  }

  if (std::strcmp(argv[1], "throw") == 0) {
    try {
      sixo_smoke_throw_site();
    } catch (...) {
      return 0;
    }
    return 1;
  }

  if (std::strcmp(argv[1], "abort") == 0) {
    sixo_smoke_abort_site();
  }

  if (std::strcmp(argv[1], "segfault") == 0) {
    sixo_smoke_segfault_site();
  }

  return 2;
}
