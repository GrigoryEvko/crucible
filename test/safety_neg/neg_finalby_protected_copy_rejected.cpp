// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copying an instance of a class that inherits from
// `crucible::safety::FinalBy<Derived>`.  FinalBy<Derived> DELETES
// its copy ctor with the named reason "[FinalBy_Subclass_Forbidden]
// FinalBy<Derived>: copy of the CRTP base is forbidden; subclassing
// a FinalBy-protected type is not allowed".  Since `Derived`'s
// implicit (defaulted) copy ctor must invoke its FinalBy base's
// copy ctor, the deletion propagates: every copy attempt on a
// FinalBy-protected `Derived` instance is a compile error.
//
// Discipline rationale (NotInherited.h:160-163):
//   FinalBy's protection works at TWO mechanism layers, and HS14
//   demands a witness for EACH:
//     - Layer 1 (existing fixture neg_finalby_subclass_attempt):
//       PRIVATE virtual base ctor inaccessible from a subclass that
//       becomes the most-derived under virtual inheritance.
//     - Layer 2 (THIS file):
//       DELETED copy/move on the CRTP base.  Even when no subclass
//       is involved, a Derived instance cannot be copied — the
//       defaulted Derived(const Derived&) would call FinalBy's
//       deleted copy, surfacing the named tag in the diagnostic.
//
//   Without layer 2, a refactor that silently changed the deleted
//   copy ctor from `delete("reason")` to `= default` would compile
//   cleanly and silently re-enable copy of every FinalBy-protected
//   value (e.g. via `T x = std::move(other)` patterns).  Layer 1's
//   subclass-attempt fixture would NOT catch that drift because the
//   subclass attack vector hits the private ctor, not the deleted
//   copy.
//
// HS14 — distinct-class fixture pair for FinalBy:
//   * Class T-subclass (sibling): private-virtual-ctor enforcement
//     against subclasses that try to become most-derived.
//   * Class T-copy (THIS file):   deleted-copy enforcement against
//     value-level copy of a FinalBy-protected instance.
//
// FIXY-U-146 — bumps FinalBy from 1 → 2 fixtures (HS14 floor met).
// Companion to U-146 NotInherited (concept-rejection) and U-146
// Stale (rvalue-only-consume) — three primitives closed in one ship.

#include <crucible/safety/NotInherited.h>

namespace {
    // Production-shape consumer: a class whose extensibility is
    // protected by FinalBy<Derived>.  The header documents this as
    // "untrusted user shouldn't add `final`" — typical use is
    // protocol message envelopes, session-resource handles, etc.
    struct ProtectedMessage
        : public virtual ::crucible::safety::FinalBy<ProtectedMessage>
    {
        int payload_ = 0;
        ProtectedMessage() = default;
    };
}

// VIOLATION: ProtectedMessage's defaulted copy ctor invokes
// FinalBy<ProtectedMessage>::FinalBy(const FinalBy&) which is
// `delete("[FinalBy_Subclass_Forbidden] FinalBy<Derived>: copy of
// the CRTP base is forbidden; ...")`.  GCC emits "use of deleted
// function" with the named tag in the reason string.
[[maybe_unused]] static ProtectedMessage offending_protected_copy(
    const ProtectedMessage& source)
{
    return ProtectedMessage{source};   // ERROR: copy ctor deleted
}

int main() { return 0; }
