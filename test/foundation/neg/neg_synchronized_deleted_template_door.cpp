// Negative-compile test. This file must fail to compile.
//
// A class that states synchronized{} holds a plain count and a deleted member
// function template, the deleted twin of a drain.  GCC 16 gives no is_deleted
// of a template, and can_substitute gives no specialization of a deleted
// template, so the check cannot read the template and fails closed (the doors
// of a synchronized class of foundation/Transfer.h).  The control of the
// check file writes the deleted form as an overload that is no template.
//
// Expected diagnostic: the template has no specialization for the probe body,
// the diagnostic names the route of a deleted form, and it names the template
// drain.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    [[nodiscard]] int read() const noexcept { return count_; }

    template <class Body>
    void drain(Body&& body) = delete("a drain of a count of two threads takes the key of the count");

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
