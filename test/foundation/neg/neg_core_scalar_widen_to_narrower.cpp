// widen has no check: a conversion to a narrower type takes narrow, which
// gives an Option.

#include <foundation/core/Convert.h>

#include <cstdint>

int main() { return ::foundation::core::widen<std::uint8_t>(300u) == 44 ? 0 : 1; }
