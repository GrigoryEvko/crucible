// The elements of a temporary LiveList end with the full expression, so a
// temporary LiveList gives no prefix.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>

class LogWriter;
struct AppendRole final {};
using Numbers = ::foundation::core::LiveList<int, ::foundation::SoleWriterProof<LogWriter, AppendRole>>;

int main() { return static_cast<int>(Numbers{}.prefix().size()); }
