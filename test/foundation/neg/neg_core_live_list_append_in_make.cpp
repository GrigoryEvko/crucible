// The make of append_with appends to the LiveList that it builds an element
// for.  The two appends would take one slot.  append_with carries the annotation
// borrow_match, and the rule borrow-match refuses a call of a member of the
// list that is not const in make.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct AppendRole final {};

class LogWriter;
using Proof = ::foundation::SoleWriterProof<LogWriter, AppendRole>;
using Numbers = ::foundation::core::LiveList<int, Proof>;

class LogWriter final {
public:
    static void append(Numbers& list) {
        ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
        list.append_with(Proof{}, alloc, [&](std::size_t) noexcept {
            list.append(Proof{}, alloc, 2);
            return 1;
        });
    }
};

int main() {
    Numbers list;
    LogWriter::append(list);
    return static_cast<int>(list.prefix().size());
}
