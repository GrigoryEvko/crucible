// A function returns a prefix of a local LiveList.  The list ends with the
// frame of the function and frees the elements, so a read of the result reads
// freed storage.  The rule view-escape refuses the return.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>

class LogWriter;
struct AppendRole final {};
using Numbers = ::foundation::core::LiveList<int, ::foundation::SoleWriterProof<LogWriter, AppendRole>>;

namespace {

::foundation::core::SegmentPrefix<int const> prefix_of_local_list() {
    Numbers list;
    return list.prefix();
}

}  // namespace

int main() { return static_cast<int>(prefix_of_local_list().size()); }
