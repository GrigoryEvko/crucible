// A reader on another thread reads the directory of a LiveList, and a move
// would write it.  So a LiveList does not move.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>

#include <utility>

class LogWriter;
struct AppendRole final {};
using Numbers = ::foundation::core::LiveList<int, ::foundation::SoleWriterProof<LogWriter, AppendRole>>;

int main() {
    Numbers list;
    Numbers const moved{std::move(list)};
    return static_cast<int>(moved.prefix().size());
}
