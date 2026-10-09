// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A zero byte inside the literal of a thread name would cut the name at
// that byte with no error.  The constructor of ThreadName reads each byte
// in the constant evaluation, and a zero byte before the end stops it.
//
// Expected diagnostic: a call of the function that is not constexpr, which
// names the zero byte.

#include <fixy/os/ThreadTasks.h>

int main() {
    [[maybe_unused]] constexpr ::fixy::thread::ThreadName name = "drain\0stage";
    return 0;
}
