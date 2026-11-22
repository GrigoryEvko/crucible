// The rvalue match moves the payload out of the Option, and the payload ends
// when the match returns.  An arm that takes the payload by reference gives a
// view into it, and the view dangles after the match.  The rvalue match takes
// only an arm whose parameter is a value, so it refuses this arm.  The arm
// gives the View of an inline variable, because the rule view-escape refuses
// a View of its own rvalue parameter, and the fixture fails only on the
// refusal of the match.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>

using Numbers = ::foundation::core::FixedArray<int, 4>;

inline Numbers fallback_numbers{};

int main() {
    ::foundation::core::View<int, 4> const seen = ::foundation::core::Option<Numbers>::some(Numbers{1, 2, 3, 4})
                                                      .match(
                                                          [](Numbers&& numbers) noexcept {
                                                              static_cast<void>(numbers);
                                                              return fallback_numbers.view();
                                                          },
                                                          []() noexcept { return fallback_numbers.view(); });
    return seen.size() == 4 ? 0 : 1;
}
