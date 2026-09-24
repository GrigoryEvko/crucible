// ThreadNamed<Name> was an aggregate, so `ThreadNamed<"x">{}` built the
// proof that a thread carries a name on a thread that no call to
// pthread_setname_np touched.  The one constructor is private now, and
// mint_thread_name is its sole friend.  This fixture is the standing
// witness that the constructor stayed private in this tree as well.

#include <crucible/safety/ThreadName.h>

int main() {
    ::crucible::safety::ThreadNamed<"forged"> witness{};
    (void)witness;
    return 0;
}
