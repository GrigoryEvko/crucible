// A file outside Report.h writes an explicit specialization of the template
// detail::fmt_text_, which Fmt befriends.  The body gives a format text that
// the consteval check of Fmt did not read, so a report reads arguments that
// the call did not give.  The rule specialization-home refuses the
// specialization in each build, because a function template of the tree gets
// an explicit specialization only in its own file.

#include <foundation/core/Report.h>

template <>
constexpr char const* foundation::core::detail::fmt_text_<long>(Fmt<long> const&) noexcept {
    return "{} {} {}";
}

int main() { return 0; }
