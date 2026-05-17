// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-FinalBy fixture #2: a subclass of a FinalBy-protected
// Derived (via the fixy:: alias) cannot construct.
//
// Violation: FinalBy<Derived>'s ctor is private; only `Derived` is a
// friend.  Through virtual inheritance, the most-derived class
// constructs the virtual base directly — so a `Sub : public Derived`
// must construct `FinalBy<Derived>` itself, which it cannot (not a
// friend).  Routing through `fixy::struct_::FinalBy` preserves the
// reject.
//
// Expected diagnostic: substring "FinalBy_Subclass_Forbidden" or
// "private within this context".

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_finalby_subclass {

// FinalBy-protected base — only this exact class may construct
// FinalBy<TypeStructFinalBySubclassBase>.
class TypeStructFinalBySubclassBase
    : public virtual fstr::FinalBy<TypeStructFinalBySubclassBase> {
public:
    TypeStructFinalBySubclassBase() = default;
};

// Illegal subclass — most-derived for the virtual base, but not
// friend of FinalBy<...>.
class TypeStructFinalBySubclassChild : public TypeStructFinalBySubclassBase {};

}  // namespace neg_fixy_struct_finalby_subclass

int main() {
    namespace tags = neg_fixy_struct_finalby_subclass;

    // Should FAIL: child's implicit ctor cannot construct the virtual
    // FinalBy<Base> base (private ctor).
    [[maybe_unused]] tags::TypeStructFinalBySubclassChild child{};
    return 0;
}
