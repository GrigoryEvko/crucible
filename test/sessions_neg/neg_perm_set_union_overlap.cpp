// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture #10 — perm_set_union_t with overlapping operands.
//
// THE BUG (looks innocent):
//   The user composes two PermSets that happen to share a tag,
//   typically when threading two participants through the same
//   protocol — e.g., two producer roles minted with the same Tag.
//   The overlap is sometimes invisible at the call site because
//   the PermSets come from disparate mint_permissioned_session calls.
//
// WHY THE TYPE SYSTEM CATCHES IT:
//   perm_set_union_t carries a static_assert(((!contains<PS1, T2s>)
//   && ...)) at the impl level.  A CSL permission cannot be held
//   by two participants simultaneously — that's the frame rule.
//   Overlap signals a permission-flow violation caught at the
//   union site, BEFORE any handle is constructed from the bad
//   PermSet.
//
// WHY IT'S TRICKY:
//   In production, the overlap usually arises from a tag-tree
//   declaration error (two splits_into specialisations naming the
//   same child for different parents) rather than a direct
//   union call.  But the underlying check fires at any union site
//   where the operands aren't disjoint — this fixture exercises
//   the trait directly.

#include <crucible/permissions/PermSet.h>

using namespace crucible::safety::proto;

struct AlphaPerm {};
struct BetaPerm  {};

// Two PermSets that share AlphaPerm.  In a real bug this would
// happen when two seemingly-independent code paths each mint
// AlphaPerm and try to merge their type-level holdings.
using PS1 = PermSet<AlphaPerm, BetaPerm>;
using PS2 = PermSet<AlphaPerm>;

// Instantiate the union — fires the disjoint-operands assert.
using BadUnion = perm_set_union_t<PS1, PS2>;

[[maybe_unused]] BadUnion u;

int main() { return 0; }
