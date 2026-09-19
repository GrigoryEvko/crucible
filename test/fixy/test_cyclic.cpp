// Sentinel TU for fixy/Cyclic.h: the counter is free-running, the slot
// is the masked low bits, and the wrap at the type ceiling stays slot
// exact.
//
// The header's self-test pins the anchors.  This TU adds the property
// the header argues for but does not check: over a full lap and past the
// type ceiling, index() always equals raw() mod N, which is what makes
// the unmasked counter safe.

#include <fixy/Cyclic.h>

#include <cstdint>
#include <type_traits>

namespace {

using ::fixy::Cyclic;

using C8 = Cyclic<std::uint32_t, 8>;
using C4x8 = Cyclic<std::uint8_t, 4>;

static_assert(sizeof(C8) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<C8>);
static_assert(C8::capacity == 8 && C8::mask == 7);

// A capacity that is not a power of two, or is zero, is not nameable.
template <auto N>
concept CanNameCyclic = requires { typename Cyclic<std::uint32_t, N>; };
static_assert(CanNameCyclic<std::uint32_t{8}>);
static_assert(CanNameCyclic<std::uint32_t{1}>);
static_assert(!CanNameCyclic<std::uint32_t{0}>, "a zero capacity has no slots");
static_assert(!CanNameCyclic<std::uint32_t{6}>, "the mask needs a power of two");

// index() is raw() mod N at every step of a full lap and one past it.
[[nodiscard]] consteval bool index_tracks_counter_over_a_lap() noexcept {
    C8 c{};
    for (std::uint32_t step = 0; step < 3u * 8u; ++step) {
        if (c.index() != (c.raw() & C8::mask)) return false;
        if (c.index() != step % 8u) return false;
        c.advance();
    }
    return true;
}
static_assert(index_tracks_counter_over_a_lap());

// The same holds across the type's own ceiling, which is the case the
// header's wrap argument rests on: N divides 2^bits, so the low bits
// are undisturbed by the high-bit wrap.
[[nodiscard]] consteval bool wrap_past_type_ceiling_stays_slot_exact() noexcept {
    C4x8 c{static_cast<std::uint8_t>(250)};
    for (int step = 0; step < 12; ++step) {
        if (c.index() != static_cast<std::uint8_t>(c.raw() & C4x8::mask)) return false;
        c.advance();
    }
    return true;
}
static_assert(wrap_past_type_ceiling_stays_slot_exact());

// index_back(i) walks backwards from the last slot advanced past, and
// every result is a valid slot for any i.
[[nodiscard]] consteval bool index_back_is_always_a_valid_slot() noexcept {
    C8 c{};
    c.advance_by(5);
    for (std::uint32_t i = 0; i < 64u; ++i) {
        if (c.index_back(i) >= C8::capacity) return false;
    }
    return c.index_back(0) == 4u && c.index_back(1) == 3u && c.index_back(4) == 0u && c.index_back(5) == 7u;
}
static_assert(index_back_is_always_a_valid_slot());

int check_runtime_lap() {
    volatile std::uint32_t seed = 0;
    C8 c{static_cast<std::uint32_t>(seed)};
    for (std::uint32_t step = 0; step < 20u; ++step) {
        if (c.index() != step % 8u) return 10;
        if (c.index() != (c.raw() & C8::mask)) return 11;
        c.advance();
    }
    if (c.raw() != 20u) return 12;

    c.advance_by(4);
    if (c.raw() != 24u || c.index() != 0u) return 13;

    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::cyclic_self_test::runtime_smoke_test();

    if (int rc = check_runtime_lap(); rc != 0) return rc;

    return 0;
}
