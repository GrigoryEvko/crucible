// ── neg_fixy_reflect_grade_non_fn (FIXY-G8 HS14) ────────────────────
//
// Calling fixy::reflect_grade<F> on a type that is NOT a fixy::fn<>
// instantiation must trigger the IsFixyFn concept gate via static_assert
// with a clear "fixy::reflect_grade<F> requires F to be a fixy::fn<...>"
// diagnostic.  Closes the bypass where a caller could accidentally
// reflect a bare safety::fn::Fn<> (or any other type) and silently get
// a 20-axis grade vector synthesized from nowhere.

#include <crucible/fixy/Fixy.h>

namespace {

// Bare int — not a fixy::fn<> instantiation.
constexpr auto desc = ::crucible::fixy::reflect_grade<int>();

}  // namespace

int main() { return 0; }
