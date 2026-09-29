// ThreadNamed<Name> is the proof that the calling thread carries Name.
// If it were an aggregate, `ThreadNamed<"x">{}` would build the proof on a
// thread that no call to pthread_setname_np ever touched.  A consumer that
// asks for the proof in place of reading /proc back would then trust a name
// the kernel never saw.
//
// The one constructor is private, and mint_thread_name, which names the
// thread first, is the sole friend.  This fixture is the standing witness
// that the constructor stays private.

#include <fixy/os/ThreadName.h>

int main() {
    fixy::ThreadNamed<"forged"> witness{};
    (void)witness;
    return 0;
}
