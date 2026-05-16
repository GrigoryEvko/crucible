// ── neg_fixy_wire_encode_non_fn (FIXY-G6 HS14) ────────────────────────
//
// Calling wire_encode<F> with F that is NOT a fixy::fn<> instantiation
// must fail the IsFixyFn concept gate.

#include <crucible/fixy/Fixy.h>

#include <array>
#include <cstdint>
#include <span>

namespace {

std::array<std::uint8_t, 64> buf{};

constexpr std::size_t written = ::crucible::fixy::wire_encode<int>(buf);

}  // namespace

int main() { return static_cast<int>(written); }
