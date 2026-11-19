// The compile-time checks of fixy/collision/Report.h.

#include <fixy/collision/Report.h>

namespace fixy {

namespace collision {

// The vantage point of fixy/collision/Report.h.  Alone, this file covers
// the families that the header includes.  The walk units compile it again
// after each header that can add to fixy::atom::detail
// (test/layer/walk-checks.txt), and there the two checks cover the rest.
// The tag is never defined — it is an identity for the instantiation
// point, not a type anyone uses.
struct collision_header_site;

static_assert(roster_join_diagnostic<collision_header_site>().empty(), roster_join_diagnostic<collision_header_site>());
static_assert(sample_set_diagnostic<collision_header_site>().empty(), sample_set_diagnostic<collision_header_site>());

// Each atom declaration of the dispatch family and of the spawn family has
// a reading.
//
// D001 and L003 read their axes through the closed relations of
// fixy/Collision.h, and a grade with no reading stops the build where a
// binding names it.  This walk finds such a declaration before a binding
// names it.  The spawn atoms are a sample set outside all_atom_roster, and
// the roster walk of the check file of fixy/Collision.h reads only the
// instantiations that a roster names.  So the walk reads the declarations
// of each namespace: each class and each class template.  A relation
// answers for a template as for each of its specializations.  The walk
// units compile this file again after each header, and the walk then sees
// each declaration that the header adds.
//
// The answer is the first declaration with no reading, or the reflection
// of void when each one has a reading.  The check compares the answer with
// ^^void, so a failure names the declaration in the comparison that GCC
// prints.  Complexity: linear in the members of the namespace.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <std::meta::info Ns, Axis A>
[[nodiscard]] consteval std::meta::info first_declaration_without_a_reading_() noexcept {
    std::meta::info found = ^^void;
    template for (constexpr std::meta::info member :
                  std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::current()))) {
        constexpr bool is_class =
            std::meta::is_type(member) && !std::meta::is_type_alias(member) && std::meta::is_class_type(member);
        if constexpr (is_class || std::meta::is_class_template(member)) {
            constexpr bool is_read = A == Axis::CallShape
                                       ? detail::call_shape_reading_(member) != detail::CallShapeReading::Unread
                                       : detail::protocol_reading_(member) != detail::ProtocolReading::Unread;
            if (!is_read && found == ^^void) found = member;
        }
    }
    return found;
}

#pragma GCC diagnostic pop

static_assert(first_declaration_without_a_reading_<^^::fixy::atom::dispatch, Axis::CallShape>() == ^^void,
              "fixy/Collision.h: a declaration of fixy::atom::dispatch has no reading in call_shape_reading_.  Name "
              "it in the closed relation, and decide how D001 reads it.");
static_assert(first_declaration_without_a_reading_<^^::fixy::atom::spawn, Axis::Protocol>() == ^^void,
              "fixy/Collision.h: a declaration of fixy::atom::spawn has no reading in protocol_reading_.  Name it in "
              "the closed relation, and decide how L003 reads it.");

}  // namespace collision

}  // namespace fixy
