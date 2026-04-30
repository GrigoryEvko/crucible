// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F13-AUDIT (Finding B) fixture — pins the requires-clause on
// the subset_row<Mask> public alias used by
// test_computation_cache_invalidation.cpp.  Without the
// `requires (Mask < 64u)` constraint, high bits of Mask would be
// silently ignored: subset_row<63> and subset_row<127> would
// produce the SAME Row<> type (full universe), aliasing four
// high-bit-garbage masks to a single Row.  This neg fixture pins
// that any caller passing Mask >= 64 fails loudly at substitution
// time with a constraint violation, instead of producing
// surprising aliased rows.
//
// Why this matters: the F13 invalidation TU enumerates 64 distinct
// subsets exactly because the underlying Universe has 6 atoms
// (2⁶ = 64).  A future caller (e.g., a refactor that bumps
// effect_count to 7 atoms but keeps Mask=64 as the upper bound)
// would silently truncate the new high-bit cases without the
// requires-clause; the fence catches that drift at substitution
// time.
//
// This fixture mirrors the subset_row alias inline so it can be
// exercised in isolation — the test/ TU defines subset_row in an
// anonymous namespace, not exposed via a header.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// (Mask < 64u).

#include <crucible/effects/EffectRow.h>

namespace {

namespace eff = ::crucible::effects;

template <unsigned Mask, unsigned Bit, eff::Effect... Atoms>
struct subset_row_helper {
    using type = std::conditional_t<
        (Mask & (1u << Bit)) != 0u,
        typename subset_row_helper<Mask, Bit + 1u, Atoms...,
            static_cast<eff::Effect>(Bit)>::type,
        typename subset_row_helper<Mask, Bit + 1u, Atoms...>::type
    >;
};

template <unsigned Mask, eff::Effect... Atoms>
struct subset_row_helper<Mask, 6u, Atoms...> {
    using type = eff::Row<Atoms...>;
};

// Same constrained alias as in the F13 invalidation TU.
template <unsigned Mask>
    requires (Mask < 64u)
using subset_row = typename subset_row_helper<Mask, 0u>::type;

}  // namespace

int main() {
    // Mask = 64 is OUT OF RANGE — the requires-clause must reject.
    // Without the fence, subset_row<64> would produce Row<> (since
    // bit 6 is outside the Bit ∈ [0, 6) iteration), silently
    // aliasing with subset_row<0>.
    using BadType = subset_row<64u>;
    [[maybe_unused]] BadType x{};
    return 0;
}
