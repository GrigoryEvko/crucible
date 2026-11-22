// Negative-compile test. This file must fail to compile.
//
// A token that states thread_bound{} proves only that its holder is its own
// owner thread.  Each thread can hold a token of an unrelated class, so such a
// token proves nothing about the counter, and the door bump that takes it
// stays a door with no proof (the proof of a door of foundation/Transfer.h).
// The key is closed, so the tie alone refuses it (the build of a token).
//
// Expected diagnostic: a door is not const and takes no proof, and the
// diagnostic names the door bump with the unrelated key.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] UnrelatedKey final {
    UnrelatedKey() noexcept {}

public:
    UnrelatedKey(UnrelatedKey const&) = delete("an unrelated key names one holder");
    UnrelatedKey& operator=(UnrelatedKey const&) = delete("an unrelated key names one holder");
    ~UnrelatedKey() = default;
};

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    void bump(UnrelatedKey& /*key*/) noexcept { ++count_; }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
