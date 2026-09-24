// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-Pinned fixture #2: a type deriving fixy::struct_::Pinned
// rejects move construction (Pinned deletes BOTH copy AND move).
//
// Violation: Pinned<T> deletes move ctor with a named reason
// ("stable address — move would invalidate references ...").
// Routing through `fixy::struct_::Pinned` must preserve the
// `= delete` identically.
//
// Expected diagnostic: substring "stable address" / "deleted function".

#include <crucible/fixy/Struct.h>

#include <utility>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_pinned_move {

struct TypeStructPinnedMove : fstr::Pinned<TypeStructPinnedMove> {
    int data = 0;
};

}  // namespace neg_fixy_struct_pinned_move

int main() {
    namespace tags = neg_fixy_struct_pinned_move;

    tags::TypeStructPinnedMove a{};
    // Should FAIL: Pinned<T> deletes move ctor.
    tags::TypeStructPinnedMove b{std::move(a)};
    (void)b;
    return 0;
}
