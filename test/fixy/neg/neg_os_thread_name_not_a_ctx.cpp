// The name mint reads its authority from a context.  A value that is not
// a context, such as an integer, carries no authority and is refused.

#include <fixy/os/ThreadName.h>

int main() {
    [[maybe_unused]] auto const& refused = fixy::mint_thread_name<"crux-int">(7);
    return 0;
}
