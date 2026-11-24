// Negative-compile test.  This file must fail to compile.
//
// A static object holds a const Ref of the consumer side of a trace ring.  The
// const of a Ref is shallow: the Ref still writes the side.  A body with no
// capture reaches each object of static storage, so the static would let a
// body open a second frame of the keyed side, and the side can end before the
// static.  The kind loan-key-static of the rule process-wide-state read the
// const of the Ref as a const path and gave no line.  It now reads the target
// of the Ref, and the kind static-borrow of the rule view-escape refuses the
// borrow in the static.

#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>

namespace {

struct ConstRefHolder {
    ::fixy::Ref<crucible::BackgroundThread::TraceRingSide> const side;
};

[[maybe_unused]] void keep(crucible::BackgroundThread::TraceRingSide& side) {
    // must fail: a static of a const Ref of a keyed side.
    static ConstRefHolder by_const_ref{::fixy::erase_brand(::fixy::to(side))};
    static_cast<void>(by_const_ref);
}

}  // namespace

int main() { return 0; }
