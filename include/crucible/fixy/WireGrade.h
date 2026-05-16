#pragma once

// ── crucible::fixy — WireGrade.h (FIXY-G6) ─────────────────────────────
//
// Stable wire-format serialization of a fixy::fn's 20-axis grade vector.
// Federation peers, Cipher persistence, and Canopy gossip can encode a
// binding's grade once and round-trip it across process boundaries
// without re-running the compile-time engagement machinery.
//
// **Format** (little-endian, fixed prefix + per-axis records):
//
//   [opcode_count : u16]                                      // = 20
//   [(opcode : u16, payload_size : u16, payload : u8[size])]  // × 20
//
// Each per-axis record carries one `WireOpcode` enumerator and the
// runtime form of the grant's NTTP / type identity (e.g.
// `vendor_backend<NV>` → `WireOpcode::Vendor` + 1-byte payload = NV's
// underlying enum value; `accept_default_strict_for<X>` → opcode for
// strict-default of that dim + zero payload).
//
// **Opcode stability promise.**  `WireOpcode` is APPEND-ONLY.  Adding
// a new shipped grant tag allocates the next unused opcode in 16-bit
// space; existing opcodes never renumber.  Cross-version compatibility
// is therefore monotonic — a newer encoder can produce records an older
// decoder rejects via `WireGradeError::UnknownOpcode`, but it cannot
// silently corrupt an old record.
//
// **Surface.**
//
//   fixy::WireOpcode                       — append-only opcode enum.
//   fixy::WireGradeError                   — Catalog-tagged error codes.
//   fixy::wire_grade_size_v<F>             — compile-time encoded size.
//   fixy::wire_encode<F>(span)             — serialize.
//   fixy::wire_decode<F>(span)             — validate.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — every output byte written explicitly.
//   TypeSafe   — Buffer span carries (ptr, size); opcodes are typed.
//   NullSafe   — span-based API; null spans are valid empty spans.
//   MemSafe    — pure span operations; zero allocation.
//   BorrowSafe — read-only encode buffer; non-overlapping pointers.
//   ThreadSafe — pure function; no shared state.
//   LeakSafe   — no resource ownership.
//   DetSafe    — encoded bytes are bit-identical for the same F across
//                compiles (opcodes are enum values; payloads are NTTP
//                underlying values).
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §5 Phase G    — wire-format grade serialization
//   safety/Diagnostic.h                   — Catalog tag pattern
//   fixy/Reflect.h                        — companion reflection layer

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reflect.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── WireOpcode — append-only stable opcode enum ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Opcodes are assigned monotonically; the documented stability
// promise is "never renumber".  New shipped grant tags claim the next
// unused value.  Removing a grant tag removes its case arms but
// reserves the opcode forever — decoders MUST treat a removed opcode
// as `UnknownOpcode` rather than silently accept a stale record.

enum class WireOpcode : std::uint16_t {
    // ── Strict-default ack (one per dim) ──────────────────────────
    StrictDefault_Type           = 0x0001,
    StrictDefault_Refinement     = 0x0002,
    StrictDefault_Usage          = 0x0003,
    StrictDefault_Effect         = 0x0004,
    StrictDefault_Security       = 0x0005,
    StrictDefault_Protocol       = 0x0006,
    StrictDefault_Lifetime       = 0x0007,
    StrictDefault_Provenance     = 0x0008,
    StrictDefault_Trust          = 0x0009,
    StrictDefault_Representation = 0x000A,
    StrictDefault_Observability  = 0x000B,
    StrictDefault_Complexity     = 0x000C,
    StrictDefault_Precision      = 0x000D,
    StrictDefault_Space          = 0x000E,
    StrictDefault_Overflow       = 0x000F,
    StrictDefault_Mutation       = 0x0010,
    StrictDefault_Reentrancy     = 0x0011,
    StrictDefault_Size           = 0x0012,
    StrictDefault_Version        = 0x0013,
    StrictDefault_Staleness      = 0x0014,

    // ── Usage relaxations ─────────────────────────────────────────
    Usage_Affine                 = 0x0100,
    Usage_Copy                   = 0x0101,
    Usage_Ghost                  = 0x0102,
    Usage_Borrow                 = 0x0103,
    Usage_Capability             = 0x0104,

    // ── Effect (payload: Effect bitmap, u32) ──────────────────────
    Effect_With                  = 0x0200,

    // ── Security ──────────────────────────────────────────────────
    Security_Declassify          = 0x0300,
    Security_UpgradeSecret       = 0x0301,

    // ── Provenance ────────────────────────────────────────────────
    Provenance_FromSource        = 0x0400,
    Provenance_Sanitize          = 0x0401,
    Provenance_ForgePhase        = 0x0402,  // payload: ForgePhase u8

    // ── Trust ─────────────────────────────────────────────────────
    Trust_Assumed                = 0x0500,
    Trust_AssumedFor             = 0x0501,

    // ── Representation ────────────────────────────────────────────
    Repr_C                       = 0x0600,
    Repr_Packed                  = 0x0601,
    Repr_Aligned                 = 0x0602,
    Repr_Simd                    = 0x0603,
    Repr_Atomic                  = 0x0604,
    Repr_Vendor                  = 0x0605,  // payload: VendorBackend u8
    Repr_Tier                    = 0x0606,  // payload: Tolerance u8
    Repr_Transport               = 0x0607,  // payload: TransportTier u8

    // ── Complexity (payload: u64) ─────────────────────────────────
    Complexity_Constant          = 0x0700,
    Complexity_Unbounded         = 0x0701,
    Complexity_Linear            = 0x0702,
    Complexity_Quadratic         = 0x0703,

    // ── Precision ─────────────────────────────────────────────────
    Precision_F32                = 0x0800,
    Precision_F64                = 0x0801,
    Precision_Reassociate        = 0x0802,
    Precision_Higham             = 0x0803,

    // ── Space (payload: u64) ──────────────────────────────────────
    Space_Unbounded              = 0x0900,
    Space_Bounded                = 0x0901,

    // ── Overflow ──────────────────────────────────────────────────
    Overflow_Wrap                = 0x0A00,
    Overflow_Saturate            = 0x0A01,
    Overflow_Widen               = 0x0A02,

    // ── Mutation ──────────────────────────────────────────────────
    Mutation_Mutable             = 0x0B00,
    Mutation_AppendOnly          = 0x0B01,
    Mutation_Monotonic           = 0x0B02,

    // ── Reentrancy ────────────────────────────────────────────────
    Reentrancy_Reentrant         = 0x0C00,
    Reentrancy_Coroutine         = 0x0C01,

    // ── Size (payload: u64) ───────────────────────────────────────
    Size_Productive              = 0x0D00,
    Size_Sized                   = 0x0D01,

    // ── Version (payload: u32) ────────────────────────────────────
    Version_V                    = 0x0E00,

    // ── Staleness (payload: u64) ──────────────────────────────────
    Staleness_StaleTo            = 0x0F00,

    // ── Refinement (payload: u64 type-name hash) ──────────────────
    Refinement_With              = 0x1000,

    // ── Protocol (payload: u64 type-name hash) ────────────────────
    Protocol_Session             = 0x1100,

    // ── Lifetime (payload: u64 region-tag hash) ───────────────────
    Lifetime_Region              = 0x1200,

    // ── Type relaxation (payload: u64 type-name hash) ─────────────
    Type_Typed                   = 0x1300,

    // ── Observability ─────────────────────────────────────────────
    Observability_Visible        = 0x1400,

    // ─── FIXY-G9: Witness opcodes (0x8000+) ─────────────────────────
    //
    // After each grant opcode, the encoder emits ONE witness opcode
    // naming the grant's witness tier.  Decoders that don't know
    // about witness opcodes hit `UnknownOpcode` cleanly; decoders that
    // DO check the witness tier against the binding's declared
    // `witness_t` for that grant.  Tier-only encoding (4 opcodes) is
    // sufficient — concrete TestId / CiRunId values are not wire-
    // material; only the tier matters for floor checks.
    //
    // Append-only — opcodes 0x8000-0x8003 are reserved permanently.
    Witness_Asserted         = 0x8000,
    Witness_Tested           = 0x8001,
    Witness_CrossValidated   = 0x8002,
    Witness_FormallyVerified = 0x8003,
};

// ═════════════════════════════════════════════════════════════════════
// ── WireGradeError — Catalog-tagged error codes ────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class WireGradeError : std::uint8_t {
    BufferTooSmall    = 1,
    BadOpcodeCount    = 2,
    UnknownOpcode     = 3,
    GradeMismatch     = 4,
    PayloadSizeWrong  = 5,
    TruncatedPayload  = 6,
    // FIXY-G9: witness opcode missing or below per-binding floor.
    WitnessFloor      = 7,
};

// ═════════════════════════════════════════════════════════════════════
// ── Per-grant → opcode dispatch ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// One primary template (rejected at compile time for unrecognized
// grants — keeps the catalog exhaustive) plus per-grant
// specializations carrying opcode and optional payload.

namespace detail {

template <typename G>
struct grant_wire_record;  // primary — undefined

// Strict-default ack — one per dim.
template <dim::DimAxis D>
struct grant_wire_record<accept_default_strict_for<D>> {
    static consteval WireOpcode opcode() noexcept {
        if constexpr (D == dim::Type)           return WireOpcode::StrictDefault_Type;
        else if constexpr (D == dim::Refinement)     return WireOpcode::StrictDefault_Refinement;
        else if constexpr (D == dim::Usage)          return WireOpcode::StrictDefault_Usage;
        else if constexpr (D == dim::Effect)         return WireOpcode::StrictDefault_Effect;
        else if constexpr (D == dim::Security)       return WireOpcode::StrictDefault_Security;
        else if constexpr (D == dim::Protocol)       return WireOpcode::StrictDefault_Protocol;
        else if constexpr (D == dim::Lifetime)       return WireOpcode::StrictDefault_Lifetime;
        else if constexpr (D == dim::Provenance)     return WireOpcode::StrictDefault_Provenance;
        else if constexpr (D == dim::Trust)          return WireOpcode::StrictDefault_Trust;
        else if constexpr (D == dim::Representation) return WireOpcode::StrictDefault_Representation;
        else if constexpr (D == dim::Observability)  return WireOpcode::StrictDefault_Observability;
        else if constexpr (D == dim::Complexity)     return WireOpcode::StrictDefault_Complexity;
        else if constexpr (D == dim::Precision)      return WireOpcode::StrictDefault_Precision;
        else if constexpr (D == dim::Space)          return WireOpcode::StrictDefault_Space;
        else if constexpr (D == dim::Overflow)       return WireOpcode::StrictDefault_Overflow;
        else if constexpr (D == dim::Mutation)       return WireOpcode::StrictDefault_Mutation;
        else if constexpr (D == dim::Reentrancy)     return WireOpcode::StrictDefault_Reentrancy;
        else if constexpr (D == dim::Size)           return WireOpcode::StrictDefault_Size;
        else if constexpr (D == dim::Version)        return WireOpcode::StrictDefault_Version;
        else                                          return WireOpcode::StrictDefault_Staleness;
    }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Usage relaxations.
template <> struct grant_wire_record<grant::affine> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Usage_Affine; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::copy> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Usage_Copy; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::ghost> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Usage_Ghost; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::borrow> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Usage_Borrow; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::capability_usage> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Usage_Capability; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Effect with<...> — payload is a 4-byte effect bitmap (1 bit per enum atom).
template <::crucible::effects::Effect... Es>
struct grant_wire_record<grant::with<Es...>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Effect_With; }
    static consteval std::uint16_t payload_size() noexcept { return 4; }
    static consteval std::array<std::uint8_t, 4> payload() noexcept {
        std::uint32_t bitmap = 0;
        ((bitmap |= (std::uint32_t{1} << static_cast<unsigned>(Es))), ...);
        return {
            static_cast<std::uint8_t>(bitmap & 0xFFu),
            static_cast<std::uint8_t>((bitmap >> 8) & 0xFFu),
            static_cast<std::uint8_t>((bitmap >> 16) & 0xFFu),
            static_cast<std::uint8_t>((bitmap >> 24) & 0xFFu),
        };
    }
};

// Vendor (Representation, payload: 1-byte VendorBackend enum).
template <grant::VendorBackend V>
struct grant_wire_record<grant::vendor_backend<V>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Vendor; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(V) };
    }
};

// Recipe tier (Representation, payload: 1-byte Tolerance enum).
template <grant::Tolerance T>
struct grant_wire_record<grant::recipe_tier<T>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Tier; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(T) };
    }
};

// Transport tier (Representation, payload: 1-byte TransportTier enum).
template <grant::TransportTier T>
struct grant_wire_record<grant::transport_tier<T>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Transport; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(T) };
    }
};

// Forge phase (Provenance, payload: 1-byte ForgePhase enum).
template <grant::ForgePhase P>
struct grant_wire_record<grant::forge_phase<P>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Provenance_ForgePhase; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(P) };
    }
};

// Representation static tags.
template <> struct grant_wire_record<grant::repr_c> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_C; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::repr_packed> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Packed; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::repr_aligned> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Aligned; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::repr_simd> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Simd; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::repr_atomic> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Atomic; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Precision static + Higham parametric.
template <> struct grant_wire_record<grant::precision_f32> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Precision_F32; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::precision_f64> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Precision_F64; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::reassociate> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Precision_Reassociate; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Overflow.
template <> struct grant_wire_record<grant::overflow_wrap> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Overflow_Wrap; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::overflow_saturate> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Overflow_Saturate; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::overflow_widen> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Overflow_Widen; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Mutation.
template <> struct grant_wire_record<grant::mutable_in_place> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Mutation_Mutable; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::append_only> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Mutation_AppendOnly; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::monotonic_advance> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Mutation_Monotonic; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Reentrancy.
template <> struct grant_wire_record<grant::reentrant> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Reentrancy_Reentrant; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::coroutine> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Reentrancy_Coroutine; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Space.
template <> struct grant_wire_record<grant::space_unbounded> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Space_Unbounded; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <auto N>
struct grant_wire_record<grant::space_bounded<N>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Space_Bounded; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

// Complexity.
template <> struct grant_wire_record<grant::complexity_constant> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Complexity_Constant; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <> struct grant_wire_record<grant::complexity_unbounded> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Complexity_Unbounded; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <auto N>
struct grant_wire_record<grant::complexity_linear<N>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Complexity_Linear; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};
template <auto N>
struct grant_wire_record<grant::complexity_quadratic<N>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Complexity_Quadratic; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

// Version.
template <std::uint32_t V>
struct grant_wire_record<grant::version<V>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Version_V; }
    static consteval std::uint16_t payload_size() noexcept { return 4; }
    static consteval std::array<std::uint8_t, 4> payload() noexcept {
        return {
            static_cast<std::uint8_t>(V & 0xFFu),
            static_cast<std::uint8_t>((V >> 8) & 0xFFu),
            static_cast<std::uint8_t>((V >> 16) & 0xFFu),
            static_cast<std::uint8_t>((V >> 24) & 0xFFu),
        };
    }
};

// Size.
template <> struct grant_wire_record<grant::productive> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Size_Productive; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};
template <auto N>
struct grant_wire_record<grant::sized<N>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Size_Sized; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

// Staleness.
template <auto N>
struct grant_wire_record<grant::stale_to<N>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Staleness_StaleTo; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

// Observability.
template <> struct grant_wire_record<grant::observability_visible> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Observability_Visible; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Refinement / Protocol / Lifetime / Type — payload is a stable
// 64-bit type-name hash (8 bytes).  This is the only path that loses
// some type identity across translation units (display_string_of can
// drift), but for federation purposes the hash is the canonical key.

template <typename Pred>
struct grant_wire_record<grant::refined_with<Pred>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Refinement_With; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<Pred>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename Proto>
struct grant_wire_record<grant::protocol_session<Proto>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Protocol_Session; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<Proto>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto RegionTag>
struct grant_wire_record<grant::lifetime_region<RegionTag>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Lifetime_Region; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        // Use the NTTP value's underlying bit pattern.  For structural
        // tags this is the tag-instance hash; for integral types this is
        // the value bits.  Same address-stable representation as the
        // grant tag, so encode/decode reflexively identifies the tag.
        std::uint64_t h = static_cast<std::uint64_t>(
            ::crucible::safety::diag::stable_type_id<decltype(RegionTag)>);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename T>
struct grant_wire_record<grant::typed<T>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Type_Typed; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<T>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename Policy>
struct grant_wire_record<grant::declassify<Policy>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Security_Declassify; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<Policy>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <> struct grant_wire_record<grant::upgrade_to_secret> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Security_UpgradeSecret; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

template <typename SourceTag>
struct grant_wire_record<grant::from_source<SourceTag>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Provenance_FromSource; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<SourceTag>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename TaintClass>
struct grant_wire_record<grant::sanitize<TaintClass>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Provenance_Sanitize; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<TaintClass>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto Rationale>
struct grant_wire_record<grant::trust_assumed<Rationale>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Trust_Assumed; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

template <typename TaintClass>
struct grant_wire_record<grant::trust_assumed_for<TaintClass>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Trust_AssumedFor; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<TaintClass>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] =
                static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto Bound>
struct grant_wire_record<grant::precision_higham<Bound>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Precision_Higham; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// ─── FIXY-G9: Evidenced grant variants → same opcode as base ─────────
//
// Each evidenced `*_e<W>` variant produces the SAME wire opcode + same
// payload bytes as its bare counterpart.  The witness opcode appended
// after the grant record (see wire_encode_impl) carries the W tier.

#define CRUCIBLE_FIXY_WIRE_E_SIMPLE(evid_name, opcode_value)                \
    template <::crucible::safety::witness::IsWitness W>                     \
    struct grant_wire_record<grant::evid_name<W>> {                         \
        static consteval WireOpcode opcode() noexcept {                     \
            return WireOpcode::opcode_value;                                \
        }                                                                   \
        static consteval std::uint16_t payload_size() noexcept { return 0; }\
        static consteval std::array<std::uint8_t, 0> payload() noexcept     \
            { return {}; }                                                  \
    }

CRUCIBLE_FIXY_WIRE_E_SIMPLE(affine_e,                Usage_Affine);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(copy_e,                  Usage_Copy);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(ghost_e,                 Usage_Ghost);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(borrow_e,                Usage_Borrow);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(capability_usage_e,      Usage_Capability);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(upgrade_to_secret_e,     Security_UpgradeSecret);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(repr_c_e,                Repr_C);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(repr_packed_e,           Repr_Packed);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(repr_aligned_e,          Repr_Aligned);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(repr_simd_e,             Repr_Simd);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(repr_atomic_e,           Repr_Atomic);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(observability_visible_e, Observability_Visible);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(complexity_constant_e,   Complexity_Constant);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(complexity_unbounded_e,  Complexity_Unbounded);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(precision_f32_e,         Precision_F32);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(precision_f64_e,         Precision_F64);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(reassociate_e,           Precision_Reassociate);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(space_unbounded_e,       Space_Unbounded);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(overflow_wrap_e,         Overflow_Wrap);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(overflow_saturate_e,     Overflow_Saturate);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(overflow_widen_e,        Overflow_Widen);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(mutable_in_place_e,      Mutation_Mutable);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(append_only_e,           Mutation_AppendOnly);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(monotonic_advance_e,     Mutation_Monotonic);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(reentrant_e,             Reentrancy_Reentrant);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(coroutine_e,             Reentrancy_Coroutine);
CRUCIBLE_FIXY_WIRE_E_SIMPLE(productive_e,            Size_Productive);

#undef CRUCIBLE_FIXY_WIRE_E_SIMPLE

// Evidenced strict-default ack — forwards to per-dim StrictDefault_*
// opcode just like its bare counterpart.

template <dim::DimAxis D, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<::crucible::fixy::accept_default_strict_for_e<D, W>> {
    static consteval WireOpcode opcode() noexcept {
        return grant_wire_record<::crucible::fixy::accept_default_strict_for<D>>::opcode();
    }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

// Parametric evidenced wire records — typed-NTTP grants with witness
// suffix.  Forward to the bare opcode + same payload as base.

template <grant::VendorBackend Backend, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::vendor_backend_e<Backend, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Vendor; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(Backend) };
    }
};

template <grant::Tolerance T, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::recipe_tier_e<T, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Tier; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(T) };
    }
};

template <grant::TransportTier T, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::transport_tier_e<T, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Repr_Transport; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(T) };
    }
};

template <grant::ForgePhase P, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::forge_phase_e<P, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Provenance_ForgePhase; }
    static consteval std::uint16_t payload_size() noexcept { return 1; }
    static consteval std::array<std::uint8_t, 1> payload() noexcept {
        return { static_cast<std::uint8_t>(P) };
    }
};

template <::crucible::safety::witness::IsWitness W,
          ::crucible::effects::Effect... Es>
struct grant_wire_record<grant::with_e<W, Es...>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Effect_With; }
    static consteval std::uint16_t payload_size() noexcept { return 4; }
    static consteval std::array<std::uint8_t, 4> payload() noexcept {
        std::uint32_t bitmap = 0;
        ((bitmap |= (std::uint32_t{1} << static_cast<unsigned>(Es))), ...);
        return {
            static_cast<std::uint8_t>(bitmap & 0xFFu),
            static_cast<std::uint8_t>((bitmap >> 8) & 0xFFu),
            static_cast<std::uint8_t>((bitmap >> 16) & 0xFFu),
            static_cast<std::uint8_t>((bitmap >> 24) & 0xFFu),
        };
    }
};

template <auto N, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::space_bounded_e<N, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Space_Bounded; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto N, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::sized_e<N, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Size_Sized; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <std::uint32_t V, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::version_e<V, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Version_V; }
    static consteval std::uint16_t payload_size() noexcept { return 4; }
    static consteval std::array<std::uint8_t, 4> payload() noexcept {
        return {
            static_cast<std::uint8_t>(V & 0xFFu),
            static_cast<std::uint8_t>((V >> 8) & 0xFFu),
            static_cast<std::uint8_t>((V >> 16) & 0xFFu),
            static_cast<std::uint8_t>((V >> 24) & 0xFFu),
        };
    }
};

template <auto N, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::stale_to_e<N, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Staleness_StaleTo; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename T, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::typed_e<T, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Type_Typed; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<T>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename Pred, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::refined_with_e<Pred, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Refinement_With; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<Pred>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename Proto, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::protocol_session_e<Proto, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Protocol_Session; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<Proto>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto RegionTag, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::lifetime_region_e<RegionTag, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Lifetime_Region; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = static_cast<std::uint64_t>(
            ::crucible::safety::diag::stable_type_id<decltype(RegionTag)>);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename Policy, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::declassify_e<Policy, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Security_Declassify; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<Policy>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename SourceTag, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::from_source_e<SourceTag, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Provenance_FromSource; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<SourceTag>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <typename TaintClass, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::sanitize_e<TaintClass, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Provenance_Sanitize; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<TaintClass>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto Rationale, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::trust_assumed_e<Rationale, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Trust_Assumed; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

template <typename TaintClass, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::trust_assumed_for_e<TaintClass, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Trust_AssumedFor; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t h = ::crucible::safety::diag::stable_type_id<TaintClass>;
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto Bound, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::precision_higham_e<Bound, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Precision_Higham; }
    static consteval std::uint16_t payload_size() noexcept { return 0; }
    static consteval std::array<std::uint8_t, 0> payload() noexcept { return {}; }
};

template <auto N, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::complexity_linear_e<N, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Complexity_Linear; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

template <auto N, ::crucible::safety::witness::IsWitness W>
struct grant_wire_record<grant::complexity_quadratic_e<N, W>> {
    static consteval WireOpcode opcode() noexcept { return WireOpcode::Complexity_Quadratic; }
    static consteval std::uint16_t payload_size() noexcept { return 8; }
    static consteval std::array<std::uint8_t, 8> payload() noexcept {
        std::uint64_t v = static_cast<std::uint64_t>(N);
        std::array<std::uint8_t, 8> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
        }
        return out;
    }
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-G9: witness tier opcode mapping ───────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// Per-witness-type → witness opcode dispatch.  Tier-only — concrete
// IDs are not wire-material at the grade level.
template <typename W>
struct witness_wire_opcode;

template <typename R>
struct witness_wire_opcode<::crucible::safety::witness::Asserted<R>> {
    static consteval WireOpcode value() noexcept { return WireOpcode::Witness_Asserted; }
};

template <auto Id>
struct witness_wire_opcode<::crucible::safety::witness::Tested<Id>> {
    static consteval WireOpcode value() noexcept { return WireOpcode::Witness_Tested; }
};

template <auto Id>
struct witness_wire_opcode<::crucible::safety::witness::CrossValidated<Id>> {
    static consteval WireOpcode value() noexcept { return WireOpcode::Witness_CrossValidated; }
};

template <typename P>
struct witness_wire_opcode<::crucible::safety::witness::FormallyVerified<P>> {
    static consteval WireOpcode value() noexcept { return WireOpcode::Witness_FormallyVerified; }
};

// PlatformBounded — the wire form forwards to the underlying witness
// tier opcode.  Platform-aware enforcement happens at the consumer's
// `witness_tier_v` query (which DOES account for active platform),
// not at the wire layer; the wire byte is purely the declared tier.
template <typename W, typename... Platforms>
struct witness_wire_opcode<::crucible::safety::witness::PlatformBounded<W, Platforms...>> {
    static consteval WireOpcode value() noexcept {
        return witness_wire_opcode<W>::value();
    }
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Encode pipeline ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// Total encoded size:
//   2 bytes prefix (opcode_count = 2*N for grants+witness pairs)
// + N × (4 + grant_payload_size)        // grant record
// + N × 4                                // witness record (zero payload)
template <typename F>
struct wire_size_impl;

template <typename T, typename... Grants>
struct wire_size_impl<::crucible::fixy::fn<T, Grants...>> {
    static consteval std::size_t compute() noexcept {
        std::size_t total = 2;  // opcode_count prefix
        ((total += std::size_t{4}
                 + static_cast<std::size_t>(grant_wire_record<Grants>::payload_size())
                 + std::size_t{4}),    // witness opcode record (4 bytes, zero payload)
         ...);
        return total;
    }
};

}  // namespace detail

template <typename F>
    requires IsFixyFn<F>
inline constexpr std::size_t wire_grade_size_v =
    detail::wire_size_impl<std::remove_cvref_t<F>>::compute();

namespace detail {

inline constexpr void write_u16(std::span<std::uint8_t> buf,
                                std::size_t offset,
                                std::uint16_t v) noexcept
{
    buf[offset]     = static_cast<std::uint8_t>(v & 0xFFu);
    buf[offset + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

[[nodiscard]] inline constexpr std::uint16_t
read_u16(std::span<const std::uint8_t> buf, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(buf[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(buf[offset + 1]) << 8));
}

template <typename T, typename... Grants>
[[nodiscard]] constexpr std::size_t
wire_encode_impl(std::span<std::uint8_t> buf) noexcept
{
    std::size_t offset = 2;
    auto emit = [&]<typename G>() constexpr {
        // Grant record.
        using R = grant_wire_record<G>;
        write_u16(buf, offset, static_cast<std::uint16_t>(R::opcode()));
        write_u16(buf, offset + 2, R::payload_size());
        offset += 4;
        constexpr auto p = R::payload();
        for (std::size_t i = 0; i < p.size(); ++i) {
            buf[offset + i] = p[i];
        }
        offset += R::payload_size();
        // FIXY-G9: witness record (4 bytes, zero payload).
        using W = typename G::witness_t;
        write_u16(buf, offset,
            static_cast<std::uint16_t>(witness_wire_opcode<W>::value()));
        write_u16(buf, offset + 2, 0);
        offset += 4;
    };
    (emit.template operator()<Grants>(), ...);
    // Opcode count = 2 * sizeof...(Grants) — grant + witness per pack member.
    write_u16(buf, 0, static_cast<std::uint16_t>(2 * sizeof...(Grants)));
    return offset;
}

// Expected per-axis record (opcode + payload) for one Grants pack.
struct ExpectedRecord {
    WireOpcode opcode{};
    std::uint16_t payload_size{};
    std::array<std::uint8_t, 8> payload{};
};

template <typename G>
[[nodiscard]] consteval ExpectedRecord expected_record_for() noexcept {
    using R = grant_wire_record<G>;
    ExpectedRecord rec{};
    rec.opcode = R::opcode();
    rec.payload_size = R::payload_size();
    constexpr auto p = R::payload();
    for (std::size_t i = 0; i < p.size() && i < 8; ++i) {
        rec.payload[i] = p[i];
    }
    return rec;
}

}  // namespace detail

template <typename F>
    requires IsFixyFn<F>
[[nodiscard]] constexpr std::size_t
wire_encode(std::span<std::uint8_t> buf) noexcept
{
    return [&]<typename T, typename... Grants>(::crucible::fixy::fn<T, Grants...>*) {
        return detail::wire_encode_impl<T, Grants...>(buf);
    }(static_cast<std::remove_cvref_t<F>*>(nullptr));
}

namespace detail {

template <typename T, typename... Grants>
[[nodiscard]] constexpr std::expected<std::monostate, WireGradeError>
wire_decode_impl(std::span<const std::uint8_t> buf) noexcept
{
    const std::size_t expected_size = wire_size_impl<::crucible::fixy::fn<T, Grants...>>::compute();
    if (buf.size() < expected_size) {
        return std::unexpected(WireGradeError::BufferTooSmall);
    }
    const std::uint16_t opcode_count = read_u16(buf, 0);
    // FIXY-G9: each grant emits TWO opcodes (grant + witness).
    if (opcode_count != static_cast<std::uint16_t>(2 * sizeof...(Grants))) {
        return std::unexpected(WireGradeError::BadOpcodeCount);
    }
    std::size_t offset = 2;
    WireGradeError out_err = WireGradeError::GradeMismatch;
    bool ok = true;
    auto check = [&]<typename G>() constexpr {
        if (!ok) return;
        // ── Grant record ───────────────────────────────────────────
        using R = grant_wire_record<G>;
        const std::uint16_t opcode = read_u16(buf, offset);
        const std::uint16_t pls    = read_u16(buf, offset + 2);
        if (opcode != static_cast<std::uint16_t>(R::opcode())) {
            ok = false;
            out_err = WireGradeError::GradeMismatch;
            return;
        }
        if (pls != R::payload_size()) {
            ok = false;
            out_err = WireGradeError::PayloadSizeWrong;
            return;
        }
        offset += 4;
        constexpr auto p = R::payload();
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (buf[offset + i] != p[i]) {
                ok = false;
                out_err = WireGradeError::GradeMismatch;
                return;
            }
        }
        offset += R::payload_size();

        // ── Witness record (FIXY-G9) ───────────────────────────────
        using W = typename G::witness_t;
        const std::uint16_t w_opcode = read_u16(buf, offset);
        const std::uint16_t w_pls    = read_u16(buf, offset + 2);
        const std::uint16_t expected_w =
            static_cast<std::uint16_t>(witness_wire_opcode<W>::value());
        if (w_opcode < 0x8000) {
            // Witness opcode slot occupied by a non-witness opcode —
            // wire was produced by a pre-G9 encoder OR is corrupt.
            ok = false;
            out_err = WireGradeError::WitnessFloor;
            return;
        }
        if (w_opcode != expected_w) {
            ok = false;
            out_err = WireGradeError::WitnessFloor;
            return;
        }
        if (w_pls != 0) {
            ok = false;
            out_err = WireGradeError::PayloadSizeWrong;
            return;
        }
        offset += 4;
    };
    (check.template operator()<Grants>(), ...);
    if (!ok) return std::unexpected(out_err);
    return std::monostate{};
}

}  // namespace detail

template <typename F>
    requires IsFixyFn<F>
[[nodiscard]] constexpr std::expected<std::monostate, WireGradeError>
wire_decode(std::span<const std::uint8_t> buf) noexcept
{
    return [&]<typename T, typename... Grants>(::crucible::fixy::fn<T, Grants...>*) {
        return detail::wire_decode_impl<T, Grants...>(buf);
    }(static_cast<std::remove_cvref_t<F>*>(nullptr));
}

// ─── Diagnostic accessor ───────────────────────────────────────────

[[nodiscard]] constexpr std::string_view
wire_grade_error_name(WireGradeError e) noexcept
{
    switch (e) {
        case WireGradeError::BufferTooSmall:   return "BufferTooSmall";
        case WireGradeError::BadOpcodeCount:   return "BadOpcodeCount";
        case WireGradeError::UnknownOpcode:    return "UnknownOpcode";
        case WireGradeError::GradeMismatch:    return "GradeMismatch";
        case WireGradeError::PayloadSizeWrong: return "PayloadSizeWrong";
        case WireGradeError::TruncatedPayload: return "TruncatedPayload";
        case WireGradeError::WitnessFloor:     return "WitnessFloor";
        default:                                return "<unknown WireGradeError>";
    }
}

}  // namespace crucible::fixy
