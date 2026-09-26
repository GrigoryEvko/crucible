// A Computation derives from its substrate privately.  The substrate's
// keyed peek_mut writes the payload of any row for any holder of a key,
// and any class can mint a key for itself, so the private base is what
// keeps it off a Computation.  A value of an engaged row is replaced only
// by building a new one through the witnessed mint.
//
// Expected diagnostic: the substrate's peek_mut is inaccessible.

#include <foundation/effects/Computation.h>

namespace {
struct outside_authority {
    [[nodiscard]] static constexpr ::foundation::algebra::grade_key<outside_authority> key() noexcept {
        return ::foundation::algebra::grade_key<outside_authority>{};
    }
};
}  // namespace

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>> const ctx{fe::testing::bg()};
    auto engaged = fe::Computation<fe::Row<>, int>::mint_computation_in_ctx<fe::Effect::Bg>(ctx, 1);
    engaged.peek_mut(outside_authority::key()) = 2;
    return 0;
}
