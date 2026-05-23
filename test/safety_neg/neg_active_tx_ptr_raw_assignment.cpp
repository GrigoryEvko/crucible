// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-6 #1065, mismatch class #1 of 2:
// RAW `Transaction*` CANNOT BE ASSIGNED DIRECTLY TO A
// `Tagged<Transaction*, source::Ring>` FIELD.
//
// `Tagged<T, S>` requires explicit construction via `Tagged<T, S>{value}`
// — the `explicit` ctor refuses implicit conversion from the raw `T`
// pointer.  This catches the most common production-side defect mode:
// a caller does `active_tx_ = some_tx` (raw assignment) when the
// migration intent was `active_tx_ = ActiveTxPtr{some_tx}` (typed
// construction).  Without this gate, a fresh-heap-allocated
// Transaction* (NOT borrowed from the log's ring slots) could leak
// into active_tx_ and the source::Ring provenance invariant
// ("interior pointer to ring_'s inline slots, valid for the log's
// lifetime") would silently degrade.
//
// Companion fixture: neg_active_tx_ptr_cross_source_assignment.cpp
//   * That one catches cross-source mixing (Tagged<...,source::Arena>
//     → Tagged<...,source::Ring>) — provenance LAUNDERING.
//   * This one catches raw-pointer admission — provenance BYPASS.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/safety/Tagged.h>

namespace crucible {
struct FakeTransaction { int dummy; };
}

int main() {
    using ActiveTxPtr = ::crucible::safety::Tagged<
        crucible::FakeTransaction*, ::crucible::safety::source::Ring>;

    crucible::FakeTransaction tx{};
    crucible::FakeTransaction* raw_ptr = &tx;

    // Should FAIL: implicit conversion from raw Transaction* to
    // Tagged<Transaction*, source::Ring> is rejected by the explicit ctor.
    // The migration intent is `ActiveTxPtr{raw_ptr}` — never `= raw_ptr`.
    ActiveTxPtr field = raw_ptr;
    (void)field;
    return 0;
}
