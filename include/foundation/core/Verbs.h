#pragma once

// The shared verbs of the language, and the check of the closed surface of a
// family type.
//
// The fifteen verbs of plan section 11.1 and the language protocols are the
// shared operations of the families.  Each public member of a family type is
// one of these three:
//
//   a shared verb     match, expect, get, at, view, size, push, append,
//                     consume, enumerate, reverse, windows and chunks, and
//                     begin and end, the members that `for (x : c)` calls.
//                     zip and the mints are free functions, so no member
//                     takes their names
//   a language protocol
//                     a constructor, the destructor, an assignment, and the
//                     compare operators == and <=>.  The dereference
//                     (operator* and operator->) and the cursor of a loop
//                     (operator* and operator++) only when the check grants
//                     them, because a type that can be empty has no
//                     dereference
//   a name of the family list
//                     the list that the check file of the family states one
//                     time, as one text of names with one space between two
//                     names.
//
// closed_surface<T, Grants...>(family_list) reads each public member of T,
// and of each public base of T, by reflection.  A public data member, a
// member type, a conversion function and each other operator need a name or
// a kind of the three lists too, so an unknown shape is refused.  The first
// member outside the lists gives a verdict that is not closed, and its text
// names the member and the type.  CRUCIBLE_CLOSED_SURFACE(family_list, T,
// Grants...) asserts the verdict, so such a member fails the build with
// that text.  The type comes after the list, so a comma in its template
// arguments needs no parentheses.
//
//   inline constexpr TextView choice_names{"none NoValue some is_some ..."};
//   CRUCIBLE_CLOSED_SURFACE(choice_names, Option<int>);
//   CRUCIBLE_CLOSED_SURFACE(choice_names, OptionCursor<int>, CursorProtocol);
//
// The walk is a template on the type that it checks, so only a unit that
// calls the check evaluates it.  Its cost is linear in the public members of
// T and its bases, and in the length of the two lists for each member.

#include <foundation/core/Text.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::core {

// The names of the shared verbs that a member can take.
inline constexpr TextView shared_verbs{
    "match expect get at view size push append consume enumerate reverse windows chunks begin end"};

// The grant of the dereference: operator* and operator->.  Only a type with
// no empty state takes it, such as Ref, Box and CacheLine.
struct DereferenceProtocol final {};

// The grant of the cursor of a loop: operator* and operator++.
struct CursorProtocol final {};

// A grant of a language protocol that the check knows.  An unknown grant is
// refused, so a check cannot grant a protocol that the language does not have.
template <class Grant>
concept LanguageProtocolGrant = std::is_same_v<Grant, DereferenceProtocol> || std::is_same_v<Grant, CursorProtocol>;

// The answer of the check.  A closed verdict has an empty text.  Each other
// verdict names the first public member outside the three lists.  data() and
// size() give the text, which static_assert prints as its message, and view()
// gives it as a TextView.
class SurfaceVerdict {
    // The last byte of the buffer stays zero, so a zero follows the text.
    static constexpr std::size_t capacity_ = 512;

    char text_[capacity_]{};
    std::size_t size_ = 0;

    // Appends the characters of text.  A text past the capacity stops one
    // byte before it.
    consteval void append_(TextView text) noexcept {
        char const* const first = detail::text_first_(text);
        for (std::size_t index = 0; index < text.size() && size_ + 1 < capacity_; ++index) {
            text_[size_] = first[index];
            ++size_;
        }
    }

public:
    // The closed verdict.
    consteval SurfaceVerdict() noexcept = default;

    // The verdict that refuses a public member of a checked type, from the
    // texts that name the two.
    consteval SurfaceVerdict(TextView member, TextView checked) noexcept {
        append_("the public member ");
        append_(member);
        append_(" of ");
        append_(checked);
        append_(" is no shared verb, no language protocol and no name of the family list");
    }

    [[nodiscard]] constexpr bool is_closed() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr char const* data() const noexcept { return text_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

    // The text.  The view borrows this verdict, so a view of a temporary
    // verdict is refused: it would dangle at the end of the full expression.
    [[nodiscard]] constexpr TextView view() const& noexcept { return detail::c_text_over_(text_, size_).view(); }
    TextView view() const&& = delete("a view of a temporary SurfaceVerdict points into an object that the full "
                                     "expression ends");
};

namespace detail {

// True when word is one of the names of list.  One space divides two names
// of list.  Complexity: linear in the length of list.
[[nodiscard]] consteval bool lists_name_(TextView list, TextView word) noexcept {
    char const* const text = text_first_(list);
    std::size_t const size = list.size();
    char const* const wanted = text_first_(word);
    std::size_t start = 0;
    while (start < size) {
        std::size_t stop = start;
        while (stop < size && text[stop] != ' ')
            ++stop;
        bool is_same = stop - start == word.size();
        for (std::size_t index = 0; is_same && index < word.size(); ++index) {
            is_same = text[start + index] == wanted[index];
        }
        if (is_same) return true;
        start = stop + 1;
    }
    return false;
}

// True when member is a language protocol: a constructor, the destructor, an
// assignment, == or <=>, or a granted dereference or cursor operator.
[[nodiscard]] consteval bool is_language_protocol_(std::meta::info member, bool grants_dereference,
                                                   bool grants_cursor) {
    if (std::meta::is_constructor(member) || std::meta::is_constructor_template(member)
        || std::meta::is_destructor(member)) {
        return true;
    }
    if (!std::meta::is_operator_function(member) && !std::meta::is_operator_function_template(member)) return false;
    switch (std::meta::operator_of(member)) {
        case std::meta::operators::op_equals:
        case std::meta::operators::op_equals_equals:
        case std::meta::operators::op_spaceship:
            return true;
        case std::meta::operators::op_star:
            return grants_dereference || grants_cursor;
        case std::meta::operators::op_arrow:
            return grants_dereference;
        case std::meta::operators::op_plus_plus:
            return grants_cursor;
        default:
            return false;
    }
}

// True when member has no name of its own that a list can hold: an operator
// that the grants do not give, a conversion function, or an unnamed member.
[[nodiscard]] consteval bool has_no_listed_name_(std::meta::info member) {
    return std::meta::is_operator_function(member) || std::meta::is_operator_function_template(member)
        || std::meta::is_conversion_function(member) || std::meta::is_conversion_function_template(member)
        || !std::meta::has_identifier(member);
}

// A static text of a text that reflection gives.  The static string has a
// zero after its last character and no zero before it, because a reflected
// name holds no zero, so it is a CText.  Anchor is the checked type, so a
// call that names it depends on the template of the walk, and only a unit
// that runs the check instantiates std::define_static_string.  Complexity:
// linear in the length of the text.
template <std::meta::info Anchor, class Text>
[[nodiscard]] consteval TextView static_text_(Text const& text) {
    char const* const first = std::define_static_string(text);
    std::size_t size = 0;
    while (first[size] != '\0')
        ++size;
    return c_text_over_(first, size).view();
}

// The verdict of the public members of type, and of its public bases, for
// the checked type.  The context of no class reads only the public members
// and the public bases.  Complexity: linear in the public members of type
// and its bases, and in the length of the lists for each member.
template <std::meta::info Checked>
consteval SurfaceVerdict surface_verdict_(std::meta::info type, TextView family_list, bool grants_dereference,
                                          bool grants_cursor) {
    std::meta::access_context const outside = std::meta::access_context::unprivileged();
    for (std::meta::info const member : std::meta::members_of(type, outside)) {
        if (is_language_protocol_(member, grants_dereference, grants_cursor)) continue;
        if (!has_no_listed_name_(member)) {
            TextView const name = static_text_<Checked>(std::meta::identifier_of(member));
            if (lists_name_(shared_verbs, name) || lists_name_(family_list, name)) continue;
        }
        return SurfaceVerdict{static_text_<Checked>(std::meta::display_string_of(member)),
                              static_text_<Checked>(std::meta::display_string_of(Checked))};
    }
    for (std::meta::info const base : std::meta::bases_of(type, outside)) {
        SurfaceVerdict const verdict =
            surface_verdict_<Checked>(std::meta::type_of(base), family_list, grants_dereference, grants_cursor);
        if (!verdict.is_closed()) return verdict;
    }
    return SurfaceVerdict{};
}

}  // namespace detail

// The verdict of the public surface of the class T against the shared verbs,
// the language protocols with the grants, and family_list.
template <class T, LanguageProtocolGrant... Grants>
    requires std::is_class_v<T>
[[nodiscard]] consteval SurfaceVerdict closed_surface(TextView family_list) {
    return detail::surface_verdict_<^^T>(^^T, family_list, (std::is_same_v<Grants, DereferenceProtocol> || ...),
                                         (std::is_same_v<Grants, CursorProtocol> || ...));
}

}  // namespace foundation::core

// Asserts that the public surface of a type is closed, and prints the member
// that is not as the message of the failure.  The arguments after the list
// are the type and its grants.
#define CRUCIBLE_CLOSED_SURFACE(family_list, ...)                                           \
    static_assert(::foundation::core::closed_surface<__VA_ARGS__>(family_list).is_closed(), \
                  ::foundation::core::closed_surface<__VA_ARGS__>(family_list))
