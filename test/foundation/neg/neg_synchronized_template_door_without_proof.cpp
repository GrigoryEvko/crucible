// Negative-compile test. This file must fail to compile.
//
// GCC 16 gives no parameters of a member function template, so the check of a
// class that states synchronized{} reads the specialization of the template
// for a probe body.  The door visit lends the plain count to its body with no
// proof of the class, and its specialization shows that (the doors of a
// synchronized class of foundation/Transfer.h).
//
// Expected diagnostic: a door is not const and takes no proof, and the
// diagnostic names the template visit.

#include <foundation/Transfer.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    template <class Body>
    void visit(Body&& body) noexcept {
        body(count_);
    }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
