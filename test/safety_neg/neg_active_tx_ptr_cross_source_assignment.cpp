// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-6 #1065, mismatch class #2 of 2:
// `Tagged<T, source::Arena>` CANNOT BE ASSIGNED TO A
// `Tagged<T, source::Ring>` FIELD WITHOUT EXPLICIT RETAG.
//
// Companion to neg_active_tx_ptr_raw_assignment.cpp.  The raw-pointer
// fixture catches a caller bypassing the provenance gate entirely
// (passing a `T*` directly).  THIS fixture catches the SUBTLER defect
// mode: provenance LAUNDERING via cross-source mixing.  A caller has
// a `Tagged<Transaction*, source::Arena>` (e.g. an arena-allocated
// throwaway Transaction built for a test fixture), and tries to
// assign it to the `Tagged<Transaction*, source::Ring>` field.  Both
// are Tagged<Transaction*, ...>, but the Tag distinguishes them and
// the type system refuses the swap.
//
// Without this gate, a Transaction* from a different lifetime regime
// (arena: freed at arena reset) would silently take residence in
// active_tx_ and the source::Ring invariant ("valid for the log's
// lifetime via ring_'s inline slots, with the log's move/copy-deleted
// guarantee") would be subverted — the active_tx_ pointer would
// dangle as soon as the arena resets.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/safety/Tagged.h>

namespace crucible {
struct FakeTransaction { int dummy; };
}

int main() {
    using RingTx = ::crucible::safety::Tagged<
        crucible::FakeTransaction*, ::crucible::safety::source::Ring>;
    using ArenaTx = ::crucible::safety::Tagged<
        crucible::FakeTransaction*, ::crucible::safety::source::Arena>;

    crucible::FakeTransaction tx{};
    ArenaTx arena_tagged{&tx};

    // Should FAIL: Tagged<T, source::Arena> and Tagged<T, source::Ring>
    // are DISTINCT nominal types despite identical value_type T.
    // The Tag is the type-level provenance witness; cross-source
    // assignment requires explicit `Tagged<T, NewTag>{old.value()}`
    // re-wrapping (provenance is re-asserted at the call site).
    RingTx field = arena_tagged;
    (void)field;
    return 0;
}
