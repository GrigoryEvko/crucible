// Each Fmt derives from detail::FmtParts, which holds the text that the
// consteval constructor checked for the count of arguments.  A unit writes
// the parts of a text with two placeholders over the parts of a Fmt<int>
// through the base, so a report of one argument would read two.  The
// assignment of FmtParts is deleted, so the compiler refuses the write.

#include <foundation/core/Report.h>

int main() {
    foundation::core::Fmt<int> text{"one {}"};
    static_cast<foundation::core::detail::FmtParts&>(text) =
        foundation::core::detail::FmtParts{"{} and {}", 10, 2, foundation::core::Site{}};
    foundation::core::report(foundation::core::Sink::Out, text, 1);
    return 0;
}
