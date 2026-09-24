// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The payload is a class template that no roster of the payload walk
// names and that no rule of the session layer names.  The walk cannot
// say which effects it hides, and the empty row would let it pass every
// context.  The gate of mint_session stops the build, and the text names
// the payload.
//
// Expected diagnostic: the payload row walk refuses the payload.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace unclassified_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Wire {};
template <class Hidden>
struct Envelope {
    Hidden hidden;
};
using Proto = s::Send<Envelope<int>, s::End>;
}  // namespace unclassified_fixture

int main() {
    using namespace unclassified_fixture;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>> ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
