#pragma once

#include "protocol.hpp"

#include <csignal>

namespace sixo {

// Parse comma-separated signal names/numbers into out[]. Returns count.
// Recognizes SEGV, BUS, ILL, FPE, ABRT and decimal numbers.
int parse_signals(const char* list, int* out, int max_out);

const char* signal_name(int signo);

}  // namespace sixo
