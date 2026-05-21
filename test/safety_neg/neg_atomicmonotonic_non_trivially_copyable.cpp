// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating `crucible::safety::AtomicMonotonic<T, Cmp>`
// with a `T` that is NOT trivially-copyable.  The class template
// requires `std::is_trivially_copyable_v<T>` (Mutation.h:884) — the
// underlying `std::atomic<T>` is only well-defined for trivially-
// copyable types per [atomics.types.generic]/1.  GCC emits
// "constraints not satisfied" or "no type named ... in std::atomic"
// at template instantiation time.
//
// Discipline rationale (Mutation.h:883-905):
//   `AtomicMonotonic<T, Cmp>` wraps a `std::atomic<T> value_` field.
//   The C++ standard ([atomics.types.generic]/1) restricts atomic
//   instantiations to trivially-copyable T — otherwise lock-free
//   atomic operations on the storage are undefined.  Our requires
//   clause SURFACES the restriction at the wrapper layer instead of
//   letting it fail deep inside libstdc++'s std::atomic template
//   with an unreadable error chain.  The named requires-clause
//   diagnostic guides reviewers straight to the right axiom
//   (DetSafe + ThreadSafe — lock-free atomics demand trivial copy).
//
//   Structurally distinct from the sibling Pinned-copy fixture:
//   - Sibling:   ADDRESS STABILITY — Pinned-derived copy deletion
//     on a constructed AtomicMonotonic instance.
//   - This file: PAYLOAD TRIVIALITY — requires-clause rejection at
//     template instantiation when T itself isn't trivially-copyable.
//
//   Two enforcement layers, two distinct compile errors, two distinct
//   bug classes the type system catches.
//
// HS14 — distinct-class fixture pair for AtomicMonotonic:
//   * Class T-pinned-copy (sibling): Pinned-derived copy deletion —
//     address-stability discipline.
//   * Class U-non-trivially-copyable (THIS file): requires-clause
//     rejection — atomic-payload triviality discipline.
//
// FIXY-U-147 — Class U fixture for safety::AtomicMonotonic.

#include <crucible/safety/Mutation.h>

#include <string>

namespace {
    // Non-trivially-copyable type — `std::string` owns a heap buffer
    // and has a non-trivial copy ctor / destructor.  Production
    // shape: any caller who reaches for AtomicMonotonic<HighLevelType>
    // because "I want an atomic-ish counter" without realizing
    // std::atomic's trivial-copyable restriction.  Our requires
    // clause catches them at the wrapper layer.
    using NonTriviallyCopyableT = std::string;
}

// VIOLATION: AtomicMonotonic<std::string> fails the
// `requires std::is_trivially_copyable_v<T>` clause at line 884 of
// Mutation.h.  GCC emits "constraints not satisfied" naming
// AtomicMonotonic and is_trivially_copyable_v in the chain.  No
// AtomicMonotonic<std::string> type ever materializes — the rejection
// is at template-instantiation time, before any member is touched.
[[maybe_unused]] static void offending_non_trivial_payload() {
    ::crucible::safety::AtomicMonotonic<NonTriviallyCopyableT> bad{
        NonTriviallyCopyableT{}
    };   // ERROR: constraints not satisfied
    (void)bad;
}

int main() { return 0; }
