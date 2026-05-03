// ════════════════════════════════════════════════════════════════════
// example_cntp_frame — Phase 0 P0-5 / 5 (#1098)
//
// THE PATTERN: AN UNTRUSTED-INPUT WIRE FRAME WRAPPED IN Fn<>
//
// CNTP (Crucible Network Transport Protocol) frames arrive over the
// wire as raw bytes.  Before the runtime can act on the frame, the
// Vessel-side validator must:
//
//   1. Cast bytes → PacketHeader (Repr::Packed POD)
//   2. Validate magic, version, length, checksum
//   3. RETAG: source::External → source::Sanitized
//             trust::Unverified → trust::Tested
//             refinement: pred::True → pred::ValidCntpFrame
//
// Until step 3 succeeds, the frame must NOT cross any sanitized-only
// API.  The Fn<...> wrapper carries the per-axis trust state at the
// type level, so a function that demands `Source = source::Sanitized`
// rejects an unvalidated frame at the call site — not at runtime.
//
// THIS FILE: shows the BEFORE-validation Fn binding.  The
// AFTER-validation type would differ on three axes (Source, Trust,
// Refinement); the retag is a function `validate :
//   Fn<Header, pred::True, ..., External, Unverified> →
//   std::expected<Fn<Header, ValidCntpFrame, ..., Sanitized, Tested>,
//                 ValidationError>`.
//
// THE LIFETIME AXIS is also load-bearing here: the frame's storage
// is owned by a NetworkBuffer, NOT by the Fn wrapper.  Setting
// `Lifetime = lifetime::In<NetworkBufferTag>` documents that the
// Fn's address-validity is bounded by the buffer's lifetime.
//
// CONTRAST: where example_custom_kernel and example_custom_optimizer
// wrap CALLABLES (function pointers), this example wraps a DATA
// STRUCT.  The Fn aggregator is the same template; the per-axis
// grade choices reflect "data structure" rather than "callable".
// ════════════════════════════════════════════════════════════════════

#include <crucible/safety/Fn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace {

// ── Stand-in CNTP packet header ────────────────────────────────────
//
// A real CNTP frame would carry sequence numbers, peer identity,
// session keys, etc.  This example uses the minimal shape that
// demonstrates the binding pattern: a packed POD with a checksum.
//
// `[[gnu::packed]]` matches Repr::Packed at the type-system level —
// the wrapper's per-axis grade and the struct's actual memory
// layout agree, so a downstream consumer that demands packed
// representation cannot be fooled by a misaligned struct.
//
// Field order is INTENTIONALLY misaligned (u8 flags between u32
// magic and u16 version) — without [[gnu::packed]], the natural
// layout would insert 3 padding bytes after `flags` and 4 after
// `length` to align `checksum` on an 8-byte boundary, bloating
// the struct from 17 bytes to 24.  Packing matters here:
// `static_assert(sizeof(CntpHeader) == 17)` enforces the wire-
// format invariant that there is NO padding.

struct [[gnu::packed]] CntpHeader {
    std::uint32_t magic;       // 'CNTP' = 0x434E5450
    std::uint8_t  flags;       // bit0=ack, bit1=fragment, ...
    std::uint16_t version;     // wire format version
    std::uint16_t length;      // total frame length including header
    std::uint64_t checksum;    // FNV-1a over magic..length (excludes self)
};

static_assert(sizeof(CntpHeader) == 17,
    "CntpHeader must be exactly 17 bytes; packed-struct layout "
    "guarantee is load-bearing for Repr::Packed correctness — "
    "without packing, natural alignment would insert 7 bytes of "
    "padding (3 after flags, 4 after length) and bloat to 24 bytes.");

// ── A region tag for the Lifetime axis ─────────────────────────────
//
// Tags don't need to be defined types — they're phantom types whose
// only role is to discriminate one lifetime region from another at
// the type level.  The empty struct is the canonical convention.
struct NetworkBufferTag {};

// ── BEFORE-validation Fn binding (untrusted) ───────────────────────
//
// Per-axis grade choices for "raw CNTP frame just received from the
// wire, not yet validated":
//
//   Type        : CntpHeader                          — POD struct
//   Refinement  : pred::True                          — no compile-time gate yet
//   Usage       : Linear                              — frame is consumed once
//   EffectRow   : Row<>                               — pure data, no effects
//   Security    : SecLevel::Public                    — wire-visible header
//   Protocol    : proto::None                         — no session yet (handshake pending)
//   Lifetime    : lifetime::In<NetworkBufferTag>      — storage owned by buffer
//   Source      : source::External                    — UNTRUSTED — came over wire
//   Trust       : trust::Unverified                   — no validation yet
//   Repr        : ReprKind::Packed                    — matches [[gnu::packed]] above
//   Cost        : cost::Constant                      — fixed-size header
//   Precision   : precision::Exact                    — bit-exact wire format
//   Space       : space::Bounded<sizeof(CntpHeader)>  — exactly 16 bytes
//   Overflow    : OverflowMode::Trap                  — wire fields are integers
//   Mutation    : MutationMode::Immutable             — read-only after receive
//   Reentrancy  : ReentrancyMode::NonReentrant        — single owner per frame
//   Size        : size_pol::Sized<sizeof(CntpHeader)> — fixed observation depth
//   Version     : 1                                   — wire-protocol version
//   Staleness   : stale::Fresh                        — no aging on receive

using UnvalidatedCntpFrame = fn::Fn<
    CntpHeader,                                       // 1 Type
    fn::pred::True,                                   // 2 Refinement (none yet)
    fn::UsageMode::Linear,                            // 3 Usage
    fx::Row<>,                                        // 4 EffectRow (pure data)
    fn::SecLevel::Public,                             // 5 Security
    fn::proto::None,                                  // 6 Protocol
    fn::lifetime::In<NetworkBufferTag{}>,             // 7 Lifetime
    fn::source::External,                             // 8 Source — untrusted
    fn::trust::Unverified,                            // 9 Trust — pending validation
    fn::ReprKind::Packed,                             // 10 Repr
    fn::cost::Constant,                               // 11 Cost
    fn::precision::Exact,                             // 12 Precision
    fn::space::Bounded<sizeof(CntpHeader)>,           // 13 Space
    fn::OverflowMode::Trap,                           // 14 Overflow
    fn::MutationMode::Immutable,                      // 15 Mutation
    fn::ReentrancyMode::NonReentrant,                 // 16 Reentrancy
    fn::size_pol::Sized<sizeof(CntpHeader)>,          // 17 Size
    /*Version=*/1,                                    // 18 Version
    fn::stale::Fresh                                  // 19 Staleness
>;

// ── AFTER-validation Fn binding (sanitized) ───────────────────────
//
// In production, a refinement predicate would gate this:
//
//   struct ValidCntpFrame {
//     [[nodiscard]] static constexpr bool check(const CntpHeader& h) noexcept {
//       return h.magic == 0x434E5450 && h.version == 1 && h.length >= 16
//              && fnv1a_check(h);
//     }
//   };
//
// The validator function would have signature:
//
//   std::expected<ValidatedCntpFrame, CntpError>
//   validate(UnvalidatedCntpFrame&& raw);
//
// retagging Source from External → Sanitized, Trust from Unverified
// → Tested, and Refinement from pred::True → ValidCntpFrame.  We
// stub the predicate as pred::True here to keep the example focused
// on the per-axis-grade contrast; a real refinement predicate would
// run a checksum and structural check at construction time.

using ValidatedCntpFrame = fn::Fn<
    CntpHeader,
    fn::pred::True,                                   // would be ValidCntpFrame in prod
    fn::UsageMode::Linear,
    fx::Row<>,
    fn::SecLevel::Public,
    fn::proto::None,
    fn::lifetime::In<NetworkBufferTag{}>,
    fn::source::Sanitized,                            // 8  RETAG: External → Sanitized
    fn::trust::Tested,                                // 9  RETAG: Unverified → Tested
    fn::ReprKind::Packed,
    fn::cost::Constant,
    fn::precision::Exact,
    fn::space::Bounded<sizeof(CntpHeader)>,
    fn::OverflowMode::Trap,
    fn::MutationMode::Immutable,
    fn::ReentrancyMode::NonReentrant,
    fn::size_pol::Sized<sizeof(CntpHeader)>,
    1,
    fn::stale::Fresh
>;

// ── Compile-time invariants ────────────────────────────────────────
//
// The two bindings are DIFFERENT types — a function demanding
// `ValidatedCntpFrame` cannot be passed an `UnvalidatedCntpFrame`,
// and the type system enforces this at the call site.

static_assert(!std::is_same_v<UnvalidatedCntpFrame, ValidatedCntpFrame>,
    "Validated and unvalidated frames MUST be distinct types — "
    "otherwise the trust-state retag is unenforceable.");

// EBO collapse: the 16-byte CntpHeader plus 18 type-level grades
// equals 16 bytes (no per-axis runtime member added).
static_assert(sizeof(UnvalidatedCntpFrame) == sizeof(CntpHeader));
static_assert(sizeof(ValidatedCntpFrame)   == sizeof(CntpHeader));

// Source axis discriminates: only the validated form carries source::Sanitized.
static_assert(std::is_same_v<UnvalidatedCntpFrame::source_t,
                             fn::source::External>);
static_assert(std::is_same_v<ValidatedCntpFrame::source_t,
                             fn::source::Sanitized>);

// Trust axis discriminates: only the validated form carries trust::Tested.
static_assert(std::is_same_v<UnvalidatedCntpFrame::trust_t,
                             fn::trust::Unverified>);
static_assert(std::is_same_v<ValidatedCntpFrame::trust_t,
                             fn::trust::Tested>);

// Repr axis matches [[gnu::packed]] discipline.
static_assert(UnvalidatedCntpFrame::repr_v == fn::ReprKind::Packed);

}  // namespace

int main() {
    // Simulate a frame just received from the wire.  In production,
    // the bytes would come from a recv() / mmap()'d ring buffer.
    CntpHeader on_wire{};
    on_wire.magic    = 0x434E5450;  // 'CNTP'
    on_wire.flags    = 0;
    on_wire.version  = 1;
    on_wire.length   = sizeof(CntpHeader);
    on_wire.checksum = 0;           // a real frame computes FNV-1a here

    UnvalidatedCntpFrame untrusted{on_wire};

    // Packed-struct field reads require local copies — taking the
    // address of a misaligned member would trigger -Werror=address-
    // of-packed-member.
    {
        const auto magic   = untrusted.value().magic;
        const auto version = untrusted.value().version;
        const auto length  = untrusted.value().length;
        std::printf("untrusted frame: magic=0x%08X version=%u length=%u "
                    "(sizeof=%zu)\n",
                    magic, version, length,
                    sizeof(CntpHeader));
    }

    // A real validator would check magic + version + checksum and
    // return std::expected<ValidatedCntpFrame, CntpError>.  Here we
    // demonstrate the type-level retag by direct construction.
    if (untrusted.value().magic == 0x434E5450 &&
        untrusted.value().version == 1) {
        ValidatedCntpFrame trusted{untrusted.value()};
        std::printf("validated frame: source retagged External→Sanitized, "
                    "trust retagged Unverified→Tested\n");
        std::printf("ValidatedCntpFrame sizeof = %zu (== sizeof(CntpHeader) %zu)\n",
                    sizeof(ValidatedCntpFrame), sizeof(CntpHeader));
    }

    return 0;
}
