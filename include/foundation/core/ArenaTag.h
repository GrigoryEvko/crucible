#pragma once

// The kinds of a named tag of an owner of the base: the brand of an arena of
// foundation/core/BumpArena.h, and the member brand of a SlotMap of
// foundation/core/SlotMap.h.
//
//   ArenaMemberTag<Tag>   a member tag: a complete empty class with the mark
//                         arena_member_brand of foundation/Brand.h, and not a
//                         family tag.  A class nests the tag beside its one
//                         member of the owner, and the rule brand-escape of
//                         the quarantine plugin refuses a tag that does not
//                         name exactly one member
//   ArenaFamilyTag<Tag>   a family tag: a complete empty class with the mark
//                         arena_family_brand, and not a member tag.  No rule
//                         counts its owners
//   NamedArenaTag<Tag>    a member tag or a family tag
//
// The marks stand in foundation/Brand.h, and the concepts read them through
// carries_annotation of foundation/Linear.h.  Brand.h reaches that read only
// through Linear.h, which holds the reflection of the base, so the concepts
// have this header of their own, and Brand.h stays light.

#include <foundation/Brand.h>
#include <foundation/Linear.h>

namespace foundation::core {

// A member tag: a complete empty class with the annotation
// arena_member_brand, and not a family tag.
template <class Tag>
concept ArenaMemberTag = ::foundation::brand::IsFreshBrand<Tag>
                      && ::foundation::linear::carries_annotation(^^Tag, ^^::foundation::brand::arena_member_brand)
                      && !::foundation::linear::carries_annotation(^^Tag, ^^::foundation::brand::arena_family_brand);

// A family tag: a complete empty class with the annotation
// arena_family_brand, and not a member tag.
template <class Tag>
concept ArenaFamilyTag = ::foundation::brand::IsFreshBrand<Tag>
                      && ::foundation::linear::carries_annotation(^^Tag, ^^::foundation::brand::arena_family_brand)
                      && !::foundation::linear::carries_annotation(^^Tag, ^^::foundation::brand::arena_member_brand);

// A named tag: a member tag or a family tag.
template <class Tag>
concept NamedArenaTag = ArenaMemberTag<Tag> || ArenaFamilyTag<Tag>;

}  // namespace foundation::core
