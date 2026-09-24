// The step counter is the ordering identity of the log.  A copy would
// fork the counter, and two logs would mint the same step ids.

#include <fixy/session/Recording.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

int main() {
    s::SessionEventLog log;
    s::SessionEventLog copy = log;
    (void)copy;
    return 0;
}
