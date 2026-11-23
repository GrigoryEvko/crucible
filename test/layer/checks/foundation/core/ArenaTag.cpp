// The compile-time checks of foundation/core/ArenaTag.h.

#include <foundation/core/ArenaTag.h>

namespace foundation::core {

namespace detail::arena_tag_checks {

// A member tag nested in its owner, a family tag, a tag with no mark, a tag
// with both marks, and a class with a field and the member mark.  The owner
// holds the one arena of its member tag, as the rule brand-escape asks: an
// empty class that carries the mark of a branded arena stands for it.
template <class Tag>
struct[[= ::foundation::brand::branded_arena{}]] StandInArena final {};
struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Member final {};
    [[no_unique_address]] StandInArena<Member> arena{};
};
struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};
struct Unmarked final {};
struct[[= ::foundation::brand::arena_member_brand{}]][[= ::foundation::brand::arena_family_brand{}]] Both final {};
struct[[= ::foundation::brand::arena_member_brand{}]] Holding {
    int word = 0;
};

// A tag holds one of the two marks.  A tag of both marks, the brand that each
// value erases to, and a class that holds a field are no tag.
static_assert(ArenaMemberTag<Owner::Member> && !ArenaFamilyTag<Owner::Member>);
static_assert(ArenaFamilyTag<Family> && !ArenaMemberTag<Family>);
static_assert(NamedArenaTag<Owner::Member> && NamedArenaTag<Family>);
static_assert(!NamedArenaTag<Unmarked> && !NamedArenaTag<Both> && !NamedArenaTag<Holding>);
static_assert(!NamedArenaTag<::foundation::brand::DefaultBrand>);

}  // namespace detail::arena_tag_checks

}  // namespace foundation::core
