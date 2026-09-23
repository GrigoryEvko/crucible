// Two distinct predicates that imply each other through edges that do
// not narrow are two names for one predicate.  The closure refuses the
// cycle where a search from either name meets it, and the walk at the
// foot of fixy/Refined.h runs that search from every edge.
//
// The fixture plants the edges before the header, because the header
// reads the namespace as it stands at its foot.

#include <foundation/diag/FailClosed.h>

#include <type_traits>

namespace planted {

inline constexpr auto first_name = [](auto x) constexpr noexcept { return x > 3; };
inline constexpr auto second_name = [](auto x) constexpr noexcept { return x >= 4; };

}  // namespace planted

namespace fixy::refined::admitted_implications {

inline constexpr ::foundation::fail_closed::edge<std::remove_cv_t<decltype(planted::first_name)>,
                                                 std::remove_cv_t<decltype(planted::second_name)>>
    first_implies_second{};
inline constexpr ::foundation::fail_closed::edge<std::remove_cv_t<decltype(planted::second_name)>,
                                                 std::remove_cv_t<decltype(planted::first_name)>>
    second_implies_first{};

}  // namespace fixy::refined::admitted_implications

#include <fixy/Refined.h>

int main() { return 0; }
