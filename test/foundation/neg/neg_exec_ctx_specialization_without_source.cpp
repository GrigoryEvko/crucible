// An explicit specialization of ExecCtx replaces the whole class.  Any
// row spelling that no header instantiated is free to specialize, and the
// replacement below has a public default constructor and holds no
// capability source.  IsExecCtx reads the structure of the class, so the
// forged scope passes no ctx-bound gate.
//
// Expected diagnostic: mint_from_ctx has no candidate, because IsExecCtx
// refuses the forged specialization.

#include <foundation/effects/Capability.h>

namespace {
namespace fe = ::foundation::effects;
using UnusedRow = fe::Row<fe::Effect::IO, fe::Effect::Bg, fe::Effect::IO>;
}  // namespace

template <>
class foundation::effects::ExecCtx<fe::Bg, UnusedRow> {
public:
    using cap_type = fe::Bg;
    using row_type = UnusedRow;
    constexpr ExecCtx() noexcept = default;
};

int main() {
    fe::ExecCtx<fe::Bg, UnusedRow> forged{};
    [[maybe_unused]] auto io = fe::mint_from_ctx<fe::Effect::IO>(forged);
    return 0;
}
