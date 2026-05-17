// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-Pinned fixture #1: a type deriving fixy::struct_::Pinned
// rejects copy construction.
//
// Violation: Pinned<T> deletes copy ctor with a named reason
// ("stable address — address-as-identity or interior pointers ...").
// Routing through `fixy::struct_::Pinned` must preserve the
// `= delete` identically.
//
// Expected diagnostic: substring "stable address" / "deleted function".

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_pinned_copy {

// Unique per-fixture carrier (per CLAUDE.md HS14 discipline).
struct TypeStructPinnedCopy : fstr::Pinned<TypeStructPinnedCopy> {
    int data = 0;
};

}  // namespace neg_fixy_struct_pinned_copy

int main() {
    namespace tags = neg_fixy_struct_pinned_copy;

    tags::TypeStructPinnedCopy a{};
    // Should FAIL: Pinned<T> deletes copy ctor.
    tags::TypeStructPinnedCopy b = a;
    (void)b;
    return 0;
}
