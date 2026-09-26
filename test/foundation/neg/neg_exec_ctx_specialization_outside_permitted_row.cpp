// An explicit specialization of ExecCtx with the structure of the primary
// template: a private source, a private row, and the two member aliases.
// The primary template refuses a row that its source does not permit, but
// the specialization replaces that check along with the class.  IsExecCtx
// asks WellFormedExecCtx of the two arguments itself, so a background
// source that claims Init passes no ctx-bound gate.
//
// Expected diagnostic: mint_from_ctx has no candidate, because IsExecCtx
// refuses the specialization.

#include <foundation/effects/Capability.h>

namespace {
namespace fe = ::foundation::effects;
using ForeignRow = fe::Row<fe::Effect::Alloc, fe::Effect::Init>;
}  // namespace

template <>
class foundation::effects::ExecCtx<fe::Bg, ForeignRow> {
    fe::Bg cap_;
    ForeignRow row_{};

public:
    using cap_type = fe::Bg;
    using row_type = ForeignRow;
    constexpr explicit ExecCtx(fe::Bg cap) noexcept : cap_{cap} {}
};

int main() {
    fe::ExecCtx<fe::Bg, ForeignRow> forged{fe::testing::bg()};
    [[maybe_unused]] auto alloc = fe::mint_from_ctx<fe::Effect::Alloc>(forged);
    return 0;
}
