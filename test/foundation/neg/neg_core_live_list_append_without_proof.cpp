// An append into a LiveList takes the proof of its one writer, and only the
// owner of the proof makes it.  An append with no proof would be a second
// writer, so it has no overload.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/effects/Effect.h>

class LogWriter;
struct AppendRole final {};

int main() {
    ::foundation::core::LiveList<int, ::foundation::SoleWriterProof<LogWriter, AppendRole>> list;
    list.append(::foundation::effects::testing::test().alloc, 1);
    return static_cast<int>(list.prefix().size());
}
