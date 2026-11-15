// The compile-time checks of crucible/LoadError.h.

#include <crucible/LoadError.h>

#include <cstdint>

namespace crucible {

namespace detail_load_error_checks {

// Each reason has a text, and no two reasons share one text.  The walk reads
// each value of the underlying type, and enum_from gives the reasons among
// them.  A reason with no arm in the switch reaches unreachable(), which is
// no constant expression, so the build stops.  Quadratic in the number of
// values of the underlying type.
[[nodiscard]] consteval bool each_reason_has_its_own_text() {
    for (unsigned first = 0; first < 256; ++first) {
        for (LoadError const reason : ::fixy::enum_from<LoadError>(static_cast<std::uint8_t>(first))) {
            if (load_error_text(reason).size() == 0) return false;
            for (unsigned second = first + 1; second < 256; ++second) {
                for (LoadError const other : ::fixy::enum_from<LoadError>(static_cast<std::uint8_t>(second))) {
                    if (load_error_text(other) == load_error_text(reason)) return false;
                }
            }
        }
    }
    return true;
}
static_assert(each_reason_has_its_own_text());

}  // namespace detail_load_error_checks

}  // namespace crucible
