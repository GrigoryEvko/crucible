// A reader reads an element of a LiveList, maybe after the frame of the writer
// ended.  So an element holds no borrow, and a list of Views is refused.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/core/Region.h>

class LogWriter;
struct AppendRole final {};

int main() {
    // The size is the one use of the type.
    static_cast<void>(sizeof(foundation::core::LiveList<foundation::core::View<int const>,
                                                        foundation::SoleWriterProof<LogWriter, AppendRole>>));
    return 0;
}
