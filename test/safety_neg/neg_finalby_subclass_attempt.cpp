// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: declaring a class that inherits from a type protected
// by FinalBy<Derived>.  This is the ENFORCEMENT half of the
// FinalBy/NotInherited pair (companion to
// neg_notinherited_non_final.cpp which exercises the WITNESS half).
//
// Mechanism (per safety/NotInherited.h):
//   1. FinalBy<Derived>::FinalBy() is PRIVATE.
//   2. Derived is friend Derived; — only Derived may call the ctor.
//   3. `class MyType : public virtual FinalBy<MyType>` — MyType is the
//      most-derived of the virtual base, so it constructs FinalBy
//      directly via friendship.  OK.
//   4. `class Subclass : public MyType` — Subclass becomes the new
//      most-derived.  Virtual inheritance promotes base-construction
//      to the most-derived class, so Subclass must construct
//      FinalBy<MyType> itself.  But Subclass is NOT a friend of
//      FinalBy<MyType>, so the private constructor is inaccessible.
//      Compile error.
//
// The diagnostic chain mentions "FinalBy" and the named tag
// "[FinalBy_Subclass_Forbidden]" planted in the deleted-copy/move
// reasons — even when the primary diagnostic is "private within
// this context", any secondary "required from" message reaches one
// of the deleted special members and surfaces our tag.
//
// This pins the contract: a class declared `final-by-CRTP` instead
// of with the `final` keyword must be just as inextensible.  Future
// edits to FinalBy that drop the virtual-inheritance discipline (e.g.
// switching to non-virtual `public FinalBy<MyType>`) would silently
// permit subclassing — this test fires before such drift ships.
//
// Task #148 (A8-P3 FinalBy<T>/NotInherited<T>); see
// include/crucible/safety/NotInherited.h.

#include <crucible/safety/NotInherited.h>

class MyProtectedType : public virtual crucible::safety::FinalBy<MyProtectedType> {
public:
    MyProtectedType() = default;  // OK — friend Derived (= MyProtectedType).
};

// Should FAIL: virtual base FinalBy<MyProtectedType>'s private ctor
// is inaccessible from Subclass.  Diagnostic mentions "FinalBy" and
// either "private within this context" or our planted
// [FinalBy_Subclass_Forbidden] tag from the deleted copy/move
// reasons.
class Subclass : public MyProtectedType {
public:
    Subclass() = default;
};

int main() {
    // Trigger most-derived construction → private FinalBy ctor.
    Subclass s{};
    (void)s;
    return 0;
}
