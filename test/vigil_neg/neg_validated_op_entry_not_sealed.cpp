// Negative-compile test.  This file must fail to compile.
//
// Violation: the first tag of the trust ladder on an entry that is not
// sealed.
//
// The tags of the ladder and a validated op name a sealed entry, which no
// code changes from its construction to its end.  An entry that its caller
// can still write would let a write after the checks change the op that the
// type calls validated, so mint_ffi_entry takes only a sealed entry.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x1234};
    static_cast<void>(crucible::mint_ffi_entry(entry));
    return 0;
}
