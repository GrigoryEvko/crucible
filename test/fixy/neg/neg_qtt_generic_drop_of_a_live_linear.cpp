// A live Linear<T> owes a consume at its end.  The generic drop door of
// foundation/Linear.h would destroy the live wrapper in its own frame: the
// tracker reports the open obligation, and a build with no tracker drops the
// value with no report.  The deleted twin of the door refuses the wrapper.

#include <fixy/Qtt.h>

#include <foundation/Linear.h>

#include <utility>

int main() {
    fixy::Linear<int> owned = fixy::mint_linear<int>(42);
    ::foundation::linear::drop(std::move(owned));
    return 0;
}
