// Negative-compile test. This file must fail to compile.
//
// The constraint of the template door set refuses the probe body, so the check
// of the statement has no specialization to read, and it cannot see whether
// the door takes a proof.  The check fails closed and refuses the door (the
// doors of a synchronized class of foundation/Transfer.h).
//
// Expected diagnostic: the template has no specialization for the probe body,
// and the diagnostic names the template set.

#include <foundation/Transfer.h>

namespace {

template <class Value>
concept IsCount = __is_same(Value, int);

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    template <class Value>
        requires IsCount<Value>
    void set(Value value) noexcept {
        count_ = value;
    }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
