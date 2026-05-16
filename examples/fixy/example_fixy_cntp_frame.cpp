// ════════════════════════════════════════════════════════════════════
// example_fixy_cntp_frame — FIXY-E / Phase E worked example 2/4
//
// THE PATTERN: AN UNTRUSTED-INPUT WIRE FRAME, via fixy::fn
//
// Reject-by-default analogue of
//   examples/fn/example_cntp_frame.cpp
// (the substrate-direct version using `safety::fn::Fn<...>`).
//
// CNTP (Crucible Network Transport Protocol) frames arrive as raw
// bytes.  Until the Vessel-side validator runs (magic / version /
// checksum), the frame must NOT cross any sanitized-only API.
//
// THE LOAD-BEARING CONTRAST: this example wraps a DATA STRUCT
// (`CntpHeader` POD), NOT a callable.  The same fixy::fn aggregator
// works — per-axis grade choices reflect "data structure" rather
// than "callable".
//
// FIXY VOCAB GAP (documented, not a bug):
//   fixy Phase B's Trust resolver maps {Verified (strict),
//   Unverified (relaxed via trust_assumed*)}, but NOT trust::Tested.
//   The fn-version example contrasts Unverified→Tested across two
//   types; this fixy example shows ONLY the BEFORE form
//   (Unverified) and documents the AFTER-validation form for the
//   future fixy::cg::trust_after_validation tag landing in
//   Phase D+.
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>

#include <cstdint>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in CNTP packet header ────────────────────────────────────
//
// Packed POD matching Repr::Packed at the type-system level.

struct [[gnu::packed]] CntpHeader {
    std::uint32_t magic;       // 'CNTP' = 0x434E5450
    std::uint8_t  flags;
    std::uint16_t version;
    std::uint16_t length;
    std::uint64_t checksum;    // FNV-1a over magic..length
};

static_assert(sizeof(CntpHeader) == 17,
    "CntpHeader must be exactly 17 bytes — packed-struct guarantee "
    "is load-bearing for Repr::Packed correctness.");

// ── Region tag for the Lifetime axis ──────────────────────────────
struct NetworkBufferTag {};

// ── Taint-class + declass-policy tags ─────────────────────────────
//
// `cg::trust_assumed_for<TaintClass>` engages dim::Trust with a
// review-grep-able rationale (here: "untrusted wire bytes, validation
// pending").  `cg::declassify<Policy>` engages dim::Security with the
// audit trail; for CNTP headers, "Public" is correct because the
// header bytes are wire-visible by design.
struct UntrustedWireBytes_AwaitingValidation {};
struct PublicWireFormat_HeaderIsWireVisible {};

// ── fixy::fn binding — BEFORE-validation (untrusted) frame ────────
//
// 8 relaxations + 12 strict acknowledgements = 20 dims engaged.
// IsAccepted fires; ValidComposition fires inside the substrate's
// underlying Fn<...>.  A regression that drops one dim breaks the
// compile naming the missing axis.

using UnvalidatedCntpFrame = cf::fn<CntpHeader,
    // 1. Type — substrate carries CntpHeader.
    cf::accept_default_strict_for<cd::Type>,

    // 2. Refinement — pred::True; a real ValidCntpFrame predicate
    //    would gate the AFTER-validation type.
    cf::accept_default_strict_for<cd::Refinement>,

    // 3. Usage — Linear strict default is correct (frame is consumed
    //    once, passed to validator).
    cf::accept_default_strict_for<cd::Usage>,

    // 4. Effect — Row<> strict default is correct (pure data, no
    //    effects observable at the frame level itself).
    cf::accept_default_strict_for<cd::Effect>,

    // 5. Security = Public (relax via declassify<Policy>) — header
    //    bytes are wire-visible by design.  Strict default Classified
    //    would lie about the wire surface.
    cg::declassify<PublicWireFormat_HeaderIsWireVisible>,

    // 6. Protocol — proto::None; no session yet (handshake comes
    //    AFTER validation).
    cf::accept_default_strict_for<cd::Protocol>,

    // 7. Lifetime = lifetime::In<NetworkBufferTag> — the frame's
    //    storage is owned by the buffer, NOT by the fixy::fn
    //    wrapper.  Strict default Static would lie.
    cg::lifetime_region<NetworkBufferTag{}>,

    // 8. Provenance = External — UNTRUSTED, came over the wire.
    //    Strict default FromInternal would be a security lie.
    cg::from_source<::crucible::safety::source::External>,

    // 9. Trust = Unverified (relax via trust_assumed_for) — no
    //    validation has run yet.  Strict default Verified would
    //    assert a safety claim the substrate cannot prove.
    cg::trust_assumed_for<UntrustedWireBytes_AwaitingValidation>,

    // 10. Representation = Packed (relax via repr_packed) — matches
    //     [[gnu::packed]] on the struct definition.
    cg::repr_packed,

    // 11. Observability — derived from Effect row.
    cf::accept_default_strict_for<cd::Observability>,

    // 12. Complexity = Constant — fixed-size header.
    cg::complexity_constant,

    // 13. Precision — Exact strict default is correct (bit-exact
    //     wire format; no FP).
    cf::accept_default_strict_for<cd::Precision>,

    // 14. Space = Bounded<sizeof(CntpHeader)> — exactly 17 bytes.
    cg::space_bounded<sizeof(CntpHeader)>,

    // 15. Overflow — Trap strict default; wire fields are integers.
    cf::accept_default_strict_for<cd::Overflow>,

    // 16. Mutation — Immutable strict default; frame is read-only
    //     after receive.
    cf::accept_default_strict_for<cd::Mutation>,

    // 17. Reentrancy — NonReentrant strict default; single owner
    //     per frame.
    cf::accept_default_strict_for<cd::Reentrancy>,

    // 18. Size = Sized<sizeof(CntpHeader)> — fixed observation depth.
    cg::sized<sizeof(CntpHeader)>,

    // 19. Version — wire-protocol version 1; strict default.
    cf::accept_default_strict_for<cd::Version>,

    // 20. Staleness — Fresh strict default; no aging on receive.
    cf::accept_default_strict_for<cd::Staleness>
>;

// ── Compile-time invariants ────────────────────────────────────────

// EBO collapse — the 8 relaxation tags + 12 strict-acks add zero
// bytes to the 17-byte CntpHeader.
static_assert(sizeof(UnvalidatedCntpFrame) == sizeof(CntpHeader));

// The discriminating axes — the binding's "I'm untrusted" surface.
static_assert(std::is_same_v<UnvalidatedCntpFrame::source_t,
                             ::crucible::safety::source::External>,
    "UnvalidatedCntpFrame must carry source::External — a downstream "
    "API demanding source::Sanitized rejects this binding at the call "
    "site.");
static_assert(std::is_same_v<UnvalidatedCntpFrame::trust_t,
                             ::crucible::safety::trust::Unverified>,
    "UnvalidatedCntpFrame must carry trust::Unverified until the "
    "validator runs and retags to trust::Tested (Phase D+ vocab).");
static_assert(UnvalidatedCntpFrame::security_v == fn::SecLevel::Public,
    "CNTP header bytes are wire-visible; declassify<Policy> resolves "
    "Security to Public.");
static_assert(UnvalidatedCntpFrame::repr_v == fn::ReprKind::Packed,
    "Repr::Packed must agree with [[gnu::packed]] on the struct.");
static_assert(std::is_same_v<UnvalidatedCntpFrame::cost_t,
                             fn::cost::Constant>);
static_assert(std::is_same_v<UnvalidatedCntpFrame::space_t,
                             fn::space::Bounded<sizeof(CntpHeader)>>);

// Pure data — no effects.
static_assert(std::is_same_v<UnvalidatedCntpFrame::effect_row_t,
                             fx::Row<>>,
    "Frame data carries no effect row; effects belong to the "
    "callable that PROCESSES the frame, not to the frame itself.");

}  // namespace

int main() {
    CntpHeader on_wire{};
    on_wire.magic    = 0x434E5450;  // 'CNTP'
    on_wire.flags    = 0;
    on_wire.version  = 1;
    on_wire.length   = sizeof(CntpHeader);
    on_wire.checksum = 0;

    UnvalidatedCntpFrame untrusted{on_wire};

    // Packed-struct field reads require local copies (taking the
    // address of a misaligned member triggers -Werror=address-of-
    // packed-member).
    const auto magic   = untrusted.value().magic;
    const auto version = untrusted.value().version;
    const auto length  = untrusted.value().length;

    std::printf("fixy cntp_frame untrusted: "
                "magic=0x%08X version=%u length=%u (sizeof=%zu)\n",
                magic, version, length, sizeof(CntpHeader));

    std::printf("UnvalidatedCntpFrame sizeof = %zu "
                "(== sizeof(CntpHeader) %zu) "
                "[20-dim grade vector over a POD, zero runtime cost]\n",
                sizeof(UnvalidatedCntpFrame), sizeof(CntpHeader));
    return 0;
}
