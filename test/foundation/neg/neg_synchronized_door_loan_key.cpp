// Negative-compile test. This file must fail to compile.
//
// A LoanKey of a named brand opens the frames of the owners of that brand,
// and the root doors mint one on each thread.  So a LoanKey proves nothing
// about which thread owns the counter, also when the counter declares the
// brand as its own, and the door bump that takes it stays a door with no proof
// (the proof of a door of foundation/Transfer.h).
//
// Expected diagnostic: a door is not const and takes no proof, and the
// diagnostic names the door bump with the loan key.

#include <foundation/Transfer.h>
#include <foundation/core/Frame.h>

namespace {

struct CounterBrand final {};

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    using brand_type = CounterBrand;

    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    void bump(::foundation::core::LoanKey<CounterBrand>& /*key*/) noexcept { ++count_; }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
