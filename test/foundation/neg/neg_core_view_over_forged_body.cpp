// A file outside Region.h writes an explicit specialization of
// detail::view_over_.  The rules of the base refuse such a specialization,
// and this probe writes it in an opt-out region.  View names view_over_ as
// no friend, so the body of the specialization cannot reach the private
// constructor of a View and give a View twice as long as its buffer.

#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>

#include <cstddef>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged view_over_ shows that View names it as no friend")
template <>
constexpr foundation::core::View<int> foundation::core::detail::view_over_<int>(int* first,
                                                                                std::size_t count) noexcept {
    return foundation::core::View<int>{first, count * 2};
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
