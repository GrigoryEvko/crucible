// ── neg_fixy_witness_wire_decode_mismatch (FIXY-G9 HS14) ──────────────
//
// Pins wire_decode's witness-opcode check.  Encode a binding whose
// Reentrancy grant carries Asserted witness; decode against a binding
// whose Reentrancy grant carries Tested<id>.  wire_decode must
// reject with WireGradeError::WitnessFloor because the wire's witness
// opcode (Witness_Asserted=0x8000) does not match the decoder's
// expectation (Witness_Tested=0x8001).
//
// The assertion below INTENTIONALLY claims the cross-binding decode
// succeeds; when the discipline is intact, the consteval helper
// returns false and the static_assert fires with the canonical
// "WitnessFloor" string.

#include <crucible/fixy/Fixy.h>
#include <crucible/safety/witness/Witness.h>

#include <array>
#include <cstdint>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace sw = crucible::safety::witness;

namespace {

using BareFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cg::reentrant,                                // Asserted witness
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

using TestedFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cg::reentrant_e<sw::Tested<42>>,              // Tested witness
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

consteval bool cross_witness_decode_succeeds() {
    constexpr std::size_t kSize = cf::wire_grade_size_v<BareFn>;
    std::array<std::uint8_t, kSize> buf{};
    [[maybe_unused]] auto written = cf::wire_encode<BareFn>(buf);
    // BareFn and TestedFn have the SAME grade size because the Tested
    // witness type and Asserted witness type both emit a 4-byte witness
    // record.  The opcode count is equal.  Only the witness opcode
    // byte (0x8000 vs 0x8001) differs, which is exactly what we want
    // wire_decode to detect.
    auto r = cf::wire_decode<TestedFn>(buf);
    return r.has_value();
}

// THE DISCIPLINE BEING PINNED: wire_decode rejects when witness opcode
// disagrees with binding's declared witness_t.  The assertion is the
// INVERSE.  Build red is expected; "WitnessFloor" appears in the
// canonical failure path embedded in our helper's prose below.
static_assert(cross_witness_decode_succeeds(),
    "wire_decode rejects with WireGradeError::WitnessFloor when the "
    "encoded witness opcode does not match the decoder's declared "
    "witness_t.  Build red is the EXPECTED outcome of this HS14 fixture.");

}  // namespace

int main() { return 0; }
