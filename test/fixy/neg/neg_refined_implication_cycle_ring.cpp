// A cycle longer than two steps is refused like a cycle of two.  Three
// predicates with one meaning imply one another round a ring, and the
// search from the first one comes back to it on the third step.
//
// The fixture plants the edges before the header, because the relation
// reads the namespace as it stands where the header defines it.  Then
// the fixture calls the walk of fixy/Refined.h, which runs the search
// from every edge.

#include <foundation/diag/FailClosed.h>

#include <type_traits>

namespace planted {

inline constexpr auto above_three = [](auto x) constexpr noexcept { return x > 3; };
inline constexpr auto at_least_four = [](auto x) constexpr noexcept { return x >= 4; };
inline constexpr auto not_below_four = [](auto x) constexpr noexcept { return !(x < 4); };

using above_three_t = std::remove_cv_t<decltype(above_three)>;
using at_least_four_t = std::remove_cv_t<decltype(at_least_four)>;
using not_below_four_t = std::remove_cv_t<decltype(not_below_four)>;

}  // namespace planted

namespace fixy::refined::admitted_implications {

inline constexpr ::foundation::fail_closed::edge<planted::above_three_t, planted::at_least_four_t> ring_one{};
inline constexpr ::foundation::fail_closed::edge<planted::at_least_four_t, planted::not_below_four_t> ring_two{};
inline constexpr ::foundation::fail_closed::edge<planted::not_below_four_t, planted::above_three_t> ring_three{};

}  // namespace fixy::refined::admitted_implications

#include <fixy/Refined.h>

static_assert(::fixy::detail::refined_edge_walk::every_edge_holds());

int main() { return 0; }
