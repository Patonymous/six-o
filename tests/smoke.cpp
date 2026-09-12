#include <cstdlib>
#include <cstring>
#include <stdexcept>

[[gnu::noinline]] void sixo_smoke_throw_site() {
  throw std::runtime_error("smoke throw");
}

[[gnu::noinline]] void sixo_smoke_abort_site() {
  std::abort();
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

  return 2;
}
