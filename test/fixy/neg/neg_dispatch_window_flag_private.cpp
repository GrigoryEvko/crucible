// A unit that is no dispatch window writes the flag of the window of its
// thread.  With such a write, a body of the dispatch door could close the window
// of its door and open a second dispatch inside it, for a second key of one
// dispatch brand.  A private member function of the window holds the flag, so
// the write does not compile.

#include <fixy/concurrent/Dispatch.h>

int main() {
    fixy::dispatch::detail::DispatchWindow_::flag_of_thread_() = false;
    return 0;
}
