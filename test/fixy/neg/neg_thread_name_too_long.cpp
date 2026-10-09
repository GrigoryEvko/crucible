// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The Linux kernel keeps at most 15 characters of the name of a thread.  A
// longer name would be cut with no error, so a ThreadName takes a literal
// of at most 15 characters, and the literal here has 16.
//
// Expected diagnostic: no conversion from the literal to a ThreadName.

#include <fixy/os/ThreadTasks.h>

int main() {
    [[maybe_unused]] constexpr ::fixy::thread::ThreadName name = "sixteen-letters!";
    return 0;
}
