// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-NonMovable fixture #1: a type deriving
// fixy::struct_::NonMovable rejects copy construction.
//
// Violation: NonMovable<T> deletes copy ctor with a named reason
// ("exclusive ownership — copy would duplicate a singleton resource").
// Routing through `fixy::struct_::NonMovable` must preserve the
// `= delete` identically.
//
// Expected diagnostic: substring "exclusive ownership" / "deleted".

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_nonmovable_copy {

struct TypeStructNonMovableCopy : fstr::NonMovable<TypeStructNonMovableCopy> {
    int fd = 0;
};

}  // namespace neg_fixy_struct_nonmovable_copy

int main() {
    namespace tags = neg_fixy_struct_nonmovable_copy;

    tags::TypeStructNonMovableCopy a{};
    // Should FAIL: NonMovable<T> deletes copy ctor.
    tags::TypeStructNonMovableCopy b = a;
    (void)b;
    return 0;
}
