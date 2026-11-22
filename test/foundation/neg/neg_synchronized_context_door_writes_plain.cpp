// Negative-compile test.  This file must fail to compile.
//
// A door template of a class that states synchronized{} takes a context that
// owns Div, and it writes the plain count of the class with no proof.  The
// check reads the door as its specialization for a probe context of
// foundation/effects/Ctx.h, and a context proves no door, so two threads that
// call the door write the count at the same time (the probe of a context door
// of foundation/Transfer.h).  The check file of foundation/effects/Ctx.h holds
// the same door with a key, which compiles.
//
// Expected diagnostic: the door add takes no proof of the class.

#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>

namespace {

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    template <class Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>
    void add(Ctx const& /*ctx*/) noexcept {
        ++count_;
    }

private:
    int count_ = 0;
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }
