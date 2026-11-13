// A file outside Fatal.h writes an explicit specialization of the door
// detail::fmt_text_, with a body that gives a format text that the consteval
// check of Fmt did not read.  A report would then read arguments that the
// call did not give.  Each door of a Fmt takes detail::FmtParts, a class that
// is no template, so the door is no template, and the compiler refuses the
// specialization.

#include <foundation/core/Report.h>

template <>
constexpr char const* foundation::core::detail::fmt_text_(Fmt<long> const&) noexcept {
    return "{} {} {}";
}

int main() { return 0; }
