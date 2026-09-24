// The second translation unit of test_row_hash_translation_units.
//
// Generic closures come before every include, so each generic parameter
// that the headers declare takes a larger auto:N here than it takes in
// the first unit.  A row hash that folded the printed name of a closure
// moved with that counter.  The first unit compares the hashes of this
// unit with its own.

// The closures shift the counter.  Each one is used, so the compiler
// does not warn about it.
namespace row_hash_translation_units::shift {
inline constexpr auto first = [](auto value) { return value; };
inline constexpr auto second = [](auto left, auto right) { return left == right; };
inline constexpr auto third = [](auto a, auto b, auto c) { return a + b + c; };
static_assert(first(1) == 1 && second(2, 2) && third(1, 2, 3) == 6);
}  // namespace row_hash_translation_units::shift

#include "row_hash_translation_units.h"

#include <meta>

namespace row_hash_translation_units {

// A closure has no identity, whatever unit it is in.
static_assert(!::foundation::reflect::HasStableIdentity<decltype(shift::first)>);

// The positive control: a generic closure declared after the includes.
// The first unit declares the same closure in the same place, and the two
// must print different counters, or the shift above did nothing and the
// comparison proves nothing.
namespace late_in_shifted_unit {
inline constexpr auto probe = [](auto value) { return value; };
}  // namespace late_in_shifted_unit

std::string_view printed_late_closure_in_shifted_unit() noexcept {
    return std::meta::display_string_of(^^decltype(late_in_shifted_unit::probe));
}

std::array<std::uint64_t, kEntryCount> row_hashes_in_shifted_unit() noexcept { return row_hashes_seen_here(); }

}  // namespace row_hash_translation_units
