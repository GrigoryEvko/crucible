// A Security grade that the closed relation security_class_answer_of_
// does not name.
//
// Every reader of the Security grade asks that relation, and it gives no
// class for a type that it does not name.  So a grade on the axis with no
// class stops the build at the reader, rather than reading as public and
// letting the binding through.
//
// The atom catalog is closed, so no binding can carry such a grade: an
// atom declared here is refused at tier 2 before any reader runs, and a
// shipped Security atom without a class stops fixy/Atom.h itself.  The
// relation is the second wall behind the catalog, and this fixture asks
// the reader directly so that the second wall is shown to refuse too.

#include <fixy/Collision.h>

struct unlisted_level final : ::fixy::atom::atom_of<::fixy::Axis::Security> {};

int main() {
    [[maybe_unused]] constexpr auto refused = ::fixy::collision::detail::security_class_or_refuse_<unlisted_level>();
    return 0;
}
