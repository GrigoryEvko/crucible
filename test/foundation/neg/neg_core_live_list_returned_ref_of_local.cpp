// A function returns a Ref of an element of a local LiveList, through a prefix
// of it.  The list ends with the frame of the function and frees the element.
// The rule view-escape follows the Ref through the prefix to the list.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/core/Ref.h>

class LogWriter;
struct AppendRole final {};
using Numbers = ::foundation::core::LiveList<int, ::foundation::SoleWriterProof<LogWriter, AppendRole>>;

namespace {

auto ref_of_local_list() {
    Numbers list;
    return list.prefix().get(0).expect("the list holds one element");
}

}  // namespace

int main() { return *ref_of_local_list(); }
