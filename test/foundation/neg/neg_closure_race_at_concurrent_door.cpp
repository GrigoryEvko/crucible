// Negative-compile test. This file must fail to compile.
//
// A door whose bodies run at the same time on other threads refuses a body
// that writes a plain int of its caller through a reference, because two
// bodies can write the same int (foundation/Closure.h).  The message names
// the field, its type and the first part that refuses.

#include <foundation/Closure.h>

namespace {

inline constexpr ::foundation::closure::DoorPolicy concurrent_policy{::foundation::closure::Crossing::ConcurrentThreads,
                                                                     ::foundation::closure::QttGrade::One};

template <class Body>
void run_concurrently(Body const& body) noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, concurrent_policy, "run_concurrently");
    static_assert(verdict.is_admitted(), verdict);
    body();
}

struct CountsInto {
    int& count;
    void operator()() const noexcept { ++count; }
};

}  // namespace

int main() {
    int count = 0;
    run_concurrently(CountsInto{count});
    return count;
}
