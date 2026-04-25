#pragma once

// ── crucible::safety::NotInherited<T> / FinalBy<T> ──────────────────
//
// Two primitives for enforcing "this type may not be inherited" as a
// compile-time contract:
//
//   NotInherited<T>         — concept.  Satisfied iff T is a final class.
//                             Use at API boundaries where the caller must
//                             prove they passed a final type (witness form).
//
//   assert_not_inherited<T>() — consteval helper emitting the named
//                             diagnostic `[NotInherited_Not_Final]` when
//                             T is non-final.  Use in template bodies
//                             where the concept is not ergonomic.
//
//   FinalBy<Derived>        — CRTP base that MAKES `Derived` non-inheritable
//                             WITHOUT the `final` keyword, via virtual
//                             inheritance + private constructor + friend
//                             Derived.  Use when the user of the type
//                             cannot be trusted to add `final` but must
//                             not be able to extend the type either.
//
//   Axiom coverage:
//     TypeSafe — a "final" class really IS a leaf in the inheritance
//                tree; any subclass attempt is a compile error.
//     MemSafe  — polymorphic base classes without virtual destructors
//                are a UAF footgun; forbidding inheritance forbids the
//                pattern at the type level.
//
//   Runtime cost:
//     - `NotInherited<T>` and `assert_not_inherited<T>()` are pure
//       compile-time concepts; zero runtime cost.
//     - `FinalBy<Derived>` requires VIRTUAL inheritance by Derived,
//       which adds a vbase pointer/offset-table per Derived instance
//       (~8 bytes on x86_64).  This is NOT an EBO-collapsible zero-
//       cost primitive — prefer the `final` keyword when possible,
//       and reach for FinalBy only when the user must not be trusted
//       to remember `final`.  See detail::FinalByEboTag for the
//       static-assert that confirms FinalBy itself is an empty class;
//       the cost comes from the virtual-base mechanism in Derived.
//
//   Why BOTH primitives: NotInherited is the WITNESS (caller proves they
//   passed a final type); FinalBy is the ENFORCEMENT (library author
//   structurally prevents subclassing).  A caller-side witness without
//   a library-side enforcement allows the library to evolve to a non-
//   final type and break every caller; a library-side enforcement
//   without a witness makes the caller's precondition invisible.
//
// Usage — witness (concept) form:
//
//   template <NotInherited T>
//   void register_type() { ... }
//
//   // Or explicit assertion:
//   template <typename T>
//   void process(T& x) {
//       crucible::safety::assert_not_inherited<T>();
//       ...
//   }
//
// Usage — enforcement (CRTP) form.  VIRTUAL INHERITANCE IS REQUIRED:
//
//   class MyType : public virtual crucible::safety::FinalBy<MyType> {
//   public:
//       MyType() = default;   // OK — MyType is a friend of FinalBy<MyType>
//       // ... members, methods ...
//   };
//
//   class Subclass : public MyType { };      // COMPILE ERROR:
//                                             // private ctor inaccessible
//                                             // — subclass is the most-
//                                             // derived of the virtual
//                                             // base and must construct
//                                             // it directly, but is not
//                                             // a friend of FinalBy.
//
// See code_guide.md §XVI for the safety-wrapper discipline.

#include <crucible/Platform.h>

#include <type_traits>

namespace crucible::safety {

// ── NotInherited — concept witness ──────────────────────────────────
//
// A type T is "NotInherited" when it is declared `final`.  This is a
// caller-side witness: a function signature requiring NotInherited<T>
// rejects non-final T at the call site with a concept-not-satisfied
// diagnostic.
template <typename T>
concept NotInherited = std::is_final_v<T>;

// ── assert_not_inherited — named-diagnostic consteval helper ────────
//
// Fires the named `[NotInherited_Not_Final]` diagnostic at the
// invocation site when T is non-final.  Prefer this over a bare
// `static_assert(std::is_final_v<T>)` so that audit grep finds the
// constraint by its diagnostic tag.
template <typename T>
consteval void assert_not_inherited() noexcept {
    static_assert(
        std::is_final_v<T>,
        "[NotInherited_Not_Final] crucible::safety::assert_not_inherited<T>: "
        "T is not marked `final`. Either mark T `final` at its declaration, "
        "or inherit virtually from crucible::safety::FinalBy<T> to prevent "
        "extension structurally. See include/crucible/safety/NotInherited.h.");
}

// ── FinalBy<Derived> — CRTP enforcement ─────────────────────────────
//
// Inheriting `FinalBy<Derived>` VIRTUALLY makes Derived non-inheritable
// without requiring the `final` keyword.  Mechanism:
//
//   1. The constructor of FinalBy<Derived> is PRIVATE.
//   2. `Derived` is a friend of FinalBy<Derived>, so Derived may call
//      the private constructor.
//   3. Virtual inheritance (`public virtual FinalBy<Derived>`) forces
//      the MOST-DERIVED class to construct the virtual base directly,
//      bypassing any intermediate friendship.
//   4. Therefore, a class `Sub : public Derived` is the most-derived
//      and must construct FinalBy<Derived> itself — but Sub is NOT a
//      friend of FinalBy<Derived>, so the private constructor rejects.
//
// A non-virtual `FinalBy<Derived>` base would NOT prevent subclassing:
// an intermediate class (Derived) remains the constructor of its own
// direct base regardless of how many further derivations occur.  Only
// virtual inheritance promotes base-construction responsibility to the
// most-derived class, which is what enables this primitive to work.
//
// Named diagnostic: when an ill-formed `Sub : public Derived` attempts
// to construct, the compiler emits a message whose canonical wording
// is "'constexpr crucible::safety::FinalBy<Derived>::FinalBy()' is
// private within this context" (or a near variant).  The `[FinalBy_
// Subclass_Forbidden]` token is additionally planted in the reason
// string of the deleted copy/move/assign operators so any secondary
// diagnostic ("required from context", "cannot be defaulted because
// ...") carries our tag.
//
// Zero-bytes-in-base: FinalBy<Derived> itself is an empty class; the
// static_asserts below confirm this.  The virtual-inheritance
// mechanism, however, adds a per-Derived-instance vbase pointer or
// offset entry (~8 bytes on x86_64).  This is the only non-zero
// runtime cost; it is the price of making subclassing a compile error
// without requiring the `final` keyword at the declaration.
template <typename Derived>
class FinalBy {
private:
    // Private default ctor.  Only Derived (friend, below) may call.
    // Subclasses of Derived, as the most-derived of the virtual base
    // hierarchy, must construct FinalBy<Derived> themselves — but
    // cannot, because they are not friends.
    constexpr FinalBy() noexcept = default;
    ~FinalBy()                   = default;

    // Copy/move deleted with named reason.  A subclass's implicit
    // copy/move would attempt to invoke these, surfacing our tag in
    // any diagnostic chain that reaches them.
    FinalBy(const FinalBy&)            = delete("[FinalBy_Subclass_Forbidden] FinalBy<Derived>: copy of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");
    FinalBy(FinalBy&&)                 = delete("[FinalBy_Subclass_Forbidden] FinalBy<Derived>: move of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");
    FinalBy& operator=(const FinalBy&) = delete("[FinalBy_Subclass_Forbidden] FinalBy<Derived>: copy-assignment of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");
    FinalBy& operator=(FinalBy&&)      = delete("[FinalBy_Subclass_Forbidden] FinalBy<Derived>: move-assignment of the CRTP base is forbidden; subclassing a FinalBy-protected type is not allowed. Mark Derived final, or stop trying to extend it.");

    // Allow Derived — and only Derived — to instantiate us.
    friend Derived;
};

// ── Static sanity checks ────────────────────────────────────────────
//
// FinalBy<Derived> itself is empty — zero bytes as a class.  (The
// cost observed in Derived comes from virtual inheritance, not from
// FinalBy's own size.)
namespace detail {
struct FinalByEboTag {};
}  // namespace detail
static_assert(sizeof(FinalBy<detail::FinalByEboTag>) == sizeof(char),
              "FinalBy<T> must be an empty class — "
              "otherwise it would add direct bytes to Derived.");
static_assert(std::is_empty_v<FinalBy<detail::FinalByEboTag>>,
              "FinalBy<T> must satisfy std::is_empty_v.");

}  // namespace crucible::safety
