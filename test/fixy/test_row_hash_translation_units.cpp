// A row hash is a function of the type alone, so two translation units
// of one build compute one hash for one type.
//
// The second unit, test_row_hash_translation_units_shifted.cpp, declares
// generic closures before its includes.  GCC numbers each generic
// parameter across the translation unit, so every closure the headers
// declare prints a different auto:N there.  A closure predicate would
// move the hash of Refined<positive, int> with that number.  The
// predicates are named classes, and foundation/reflect/Hash.h refuses a
// closure in every id, so the two units must agree on every entry.
//
// The positive control shows that the shift is real: one closure,
// declared at the same place in both units, prints two counters.

#include "row_hash_translation_units.h"

#include <cstdio>
#include <cstdlib>
#include <meta>
#include <string_view>

namespace row_hash_translation_units {

namespace late_in_clean_unit {
inline constexpr auto probe = [](auto value) { return value; };
}  // namespace late_in_clean_unit

namespace {

// The part of a closure's printed name after its namespace, so that the
// two probes compare only on their counters.
[[nodiscard]] std::string_view unqualified(std::string_view printed) noexcept {
    const std::size_t at = printed.find("<lambda");
    return at == std::string_view::npos ? printed : printed.substr(at);
}

}  // namespace

}  // namespace row_hash_translation_units

int main() {
    namespace rh = ::row_hash_translation_units;
    int failures = 0;

    const std::string_view clean =
        rh::unqualified(std::meta::display_string_of(^^decltype(rh::late_in_clean_unit::probe)));
    const std::string_view shifted = rh::unqualified(rh::printed_late_closure_in_shifted_unit());
    if (clean == shifted) {
        std::fprintf(stderr, "positive control failed: both units print %.*s, so the shift moved no counter\n",
                     static_cast<int>(clean.size()), clean.data());
        ++failures;
    }

    const auto here = rh::row_hashes_seen_here();
    const auto there = rh::row_hashes_in_shifted_unit();
    for (std::size_t index = 0; index < rh::kEntryCount; ++index) {
        if (here[index] != there[index]) {
            std::fprintf(stderr, "entry %zu differs between the two units: 0x%016llx and 0x%016llx\n", index,
                         static_cast<unsigned long long>(here[index]), static_cast<unsigned long long>(there[index]));
            ++failures;
        }
        if (here[index] == 0) {
            std::fprintf(stderr, "entry %zu is zero, so the fold missed the type\n", index);
            ++failures;
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
