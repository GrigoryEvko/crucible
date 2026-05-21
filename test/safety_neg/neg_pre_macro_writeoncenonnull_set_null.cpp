// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `WriteOnceNonNull<T*>::set(nullptr)`.  The body
// fires `CRUCIBLE_PRE(p != nullptr)` (Mutation.h:737).  Under
// static_assert / consteval invocation, CRUCIBLE_PRE routes through
// `__builtin_trap()` which is non-constexpr and poisons the
// surrounding consteval call.
//
// Discipline rationale (Mutation.h:730-754):
//   WriteOnceNonNull<T*>'s `set(p)` body carries TWO CRUCIBLE_PRE
//   clauses — `p != nullptr` (the "non-null" half of the wrapper's
//   name) and `ptr_ == nullptr` (the "write-once" half).  THIS
//   fixture pins the non-null clause; the existing sibling
//   `neg_writeoncenonnull_non_pointer` pins the Class T-primary-
//   template rejection (uses-with-non-pointer-T).
//
//   The non-null clause is what DIFFERENTIATES WriteOnceNonNull
//   from WriteOnce<T*>: WriteOnce<T*> would silently accept nullptr
//   as a legal first set (it stores T* via std::optional<T*>;
//   optional engaged with nullptr is "set with null pointer", a
//   distinguishable state from "not set").  WriteOnceNonNull's
//   nullptr-sentinel storage MODEL means "ptr_ == nullptr" is
//   reserved for "slot empty", so publishing null is structurally
//   indistinguishable from "never set" — always a caller bug.
//   Production sites: KernelCache slot publication, Cipher tier
//   pointer publication, RegionNode::compiled atomic-after-bg-
//   compile — any one-shot publication where consumers spin on
//   ptr_ != nullptr.
//
// HS14 — distinct-class fixture pair for WriteOnceNonNull:
//   * Class T-primary-template (sibling
//     neg_writeoncenonnull_non_pointer): the primary template
//     `WriteOnceNonNull<NonPointer>` static_asserts at class-
//     instantiation time on non-pointer T — pins "this wrapper is
//     pointer-specialization only" structurally.
//   * Class M-set-null (THIS file): the T*-specialization's
//     `set(nullptr)` fires the body CRUCIBLE_PRE at consteval —
//     pins the non-null discipline (the differentiating feature
//     vs WriteOnce<T*>) at the method-body invariant level.
//
//   Distinct mismatch classes (T-primary-instantiation vs
//   M-method-body-pre) on the same wrapper.  Mirrors the
//   Monotonic Class M pair pattern (advance_backward +
//   bump_at_max) shipped under fixy-A1-007.
//
// FIXY-U-151 — closes the WriteOnceNonNull slice of #146 A8-P2
// (existing neg_writeoncenonnull_non_pointer was sole fixture;
// HS14 floor demands ≥2 with distinct mismatch classes).

#include <crucible/safety/Mutation.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    ::crucible::safety::WriteOnceNonNull<std::uint32_t*> slot;
    slot.set(nullptr);   // CRUCIBLE_PRE fires — p IS nullptr.
    return 0;
}

}  // namespace

// Expected diagnostic: "non-constant condition for static
// assertion" / "__builtin_trap" chain pointing into
// WriteOnceNonNull::set at Mutation.h:737.  If this static_assert
// evaluates successfully, the CRUCIBLE_PRE inside set() is bypassed
// or the non-null clause was weakened — investigate.
static_assert(under_test() == 0,
    "CRUCIBLE_PRE on WriteOnceNonNull::set's p != nullptr check MUST "
    "fire at consteval when set(nullptr) is invoked.  If this static_"
    "assert evaluates successfully, the non-null discipline that "
    "differentiates WriteOnceNonNull from WriteOnce<T*> has been "
    "weakened — investigate.");

int main() { return 0; }
