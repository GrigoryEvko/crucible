// A thread-local cell of a class whose base holds a pointer to a data member.
// The walk of the subobjects reads each base, so the null pointer to a member
// of the base keeps T{} from zero bytes, and the handle refuses the value.

#include <foundation/Quarantine.h>
#include <foundation/ThreadLocalRef.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a value of a cell holds a pointer to a member, which is not zero bytes")
namespace thread_local_ref_base_fixture {
struct Holder {
    int field;
};
struct HoldsMemberPointer {
    int Holder::* member;
};
struct DerivesMemberPointer : HoldsMemberPointer {};
struct CellTag {};
}  // namespace thread_local_ref_base_fixture
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace thread_local_ref_base_fixture;
    [[maybe_unused]] const ::foundation::ThreadLocalRef<CellTag, DerivesMemberPointer> cell{};
    return 0;
}
