// endpoint_transfer is the friend of every session handle that can move
// a live endpoint out without ending its record.  A translation unit that
// defines its own endpoint_transfer would take that friendship and read
// any handle.  Handle.h defines the struct beside the handle, so a second
// definition is a redefinition error.

#include <fixy/session/Handle.h>

namespace fixy::session::detail {

struct endpoint_transfer {
    template <typename Handle>
    static int steal(Handle&) noexcept {
        return 0;
    }
};

}  // namespace fixy::session::detail

int main() { return 0; }
