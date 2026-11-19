// A thread-local cell of a struct that holds a pointer to a data member.  A
// cell starts as zero bytes, and the null pointer to a data member is all
// ones on the Itanium ABI.  So T{} is not zero bytes, and the handle refuses
// the value.

#include <foundation/Quarantine.h>
#include <foundation/ThreadLocalRef.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a value of a cell holds a pointer to a member, which is not zero bytes")
namespace thread_local_ref_member_fixture {
struct Holder {
    int field;
};
struct HoldsMemberPointer {
    int Holder::* member;
};
struct CellTag {};
}  // namespace thread_local_ref_member_fixture
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace thread_local_ref_member_fixture;
    [[maybe_unused]] const ::foundation::ThreadLocalRef<CellTag, HoldsMemberPointer> cell{};
    return 0;
}
