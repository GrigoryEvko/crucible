// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-NotInherited fixture #1: assert_not_inherited<T>
// reached via the fixy:: alias rejects a non-final type with the
// named diagnostic [NotInherited_Not_Final].
//
// Violation: assert_not_inherited<T> static_asserts is_final_v<T>;
// passing a non-final type fires the named diagnostic.  Routing
// through `fixy::struct_::assert_not_inherited` must reject
// identically.
//
// Expected diagnostic: substring "NotInherited_Not_Final" /
// "not marked `final`".

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_notinherited_non_final {

// Non-final on purpose; assert_not_inherited<T> must reject.
struct TypeStructNotInheritedNonFinal {
    int x = 0;
};

}  // namespace neg_fixy_struct_notinherited_non_final

int main() {
    namespace tags = neg_fixy_struct_notinherited_non_final;

    // Should FAIL: T is not final.
    fstr::assert_not_inherited<tags::TypeStructNotInheritedNonFinal>();
    return 0;
}
