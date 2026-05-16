#pragma once

// ── crucible::fixy::stance — WireGradeV2.h (FIXY-G13) ─────────────────
//
// Stance-versioned wire format.  The pre-G13 wire format
// (`wire_encode<F>` / `wire_decode<F>`) emitted `[opcode_count u16]
// [records...]`.  V2 prefixes a 4-byte stance header:
//
//   [stance_id u16] [stance_version u16] [opcode_count u16] [records...]
//
// Decoders match the artifact's embedded `(stance_id, stance_version)`
// against the consumer's `accept_versions<S, Lo, Hi>` window.  Out-of-
// window load fails with `WireGradeError::StanceVersionUnsupported`;
// mismatched stance_id fails with `WireGradeError::StanceIdMismatch`.
//
// **Backwards compat.**  Legacy (V1) artifacts have no stance prefix.
// V2 decoders MAY accept them via `accept_legacy_stanceless<S> =
// true` opt-in; otherwise V1 load fails with `StanceVersionMissing`.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   wire_grade_v2_size_v<F>             — encoded size including stance hdr
//   wire_encode_v2<F, Stance>(span)     — encode with stance prefix
//   wire_decode_v2<F, Stance, Accept>   — decode, validate version window
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §7 G13       — temporal grade stability
//   fixy/stance/Version.h                — companion version header
//   fixy/WireGrade.h                     — V1 wire format

#include <crucible/fixy/WireGrade.h>
#include <crucible/fixy/stance/Version.h>

#include <expected>
#include <span>

namespace crucible::fixy::stance {

// 4-byte stance header before V1's [opcode_count u16].
inline constexpr std::size_t kStanceHeaderSize = 4;

template <typename F>
    requires IsFixyFn<F>
inline constexpr std::size_t wire_grade_v2_size_v =
    kStanceHeaderSize + ::crucible::fixy::wire_grade_size_v<F>;

// ═════════════════════════════════════════════════════════════════════
// ── wire_encode_v2<F, Stance> ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename F, typename Tag>
    requires IsFixyFn<F>
[[nodiscard]] constexpr std::size_t
wire_encode_v2(std::span<std::uint8_t> buf) noexcept
{
    if (buf.size() < kStanceHeaderSize) {
        return 0;
    }
    // Emit stance_id @ offset 0, stance_version @ offset 2.
    const std::uint16_t sid =
        ::crucible::fixy::stance::stance_id_of_v<Tag>;
    const std::uint16_t sv  =
        ::crucible::fixy::stance::stance_version_v<Tag>;
    ::crucible::fixy::detail::write_u16(buf, 0, sid);
    ::crucible::fixy::detail::write_u16(buf, 2, sv);
    // Delegate to V1 encoder for the body.
    auto sub = buf.subspan(kStanceHeaderSize);
    const std::size_t body =
        ::crucible::fixy::wire_encode<F>(sub);
    return kStanceHeaderSize + body;
}

// ═════════════════════════════════════════════════════════════════════
// ── wire_decode_v2<F, Tag, Accept> ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename F, typename Tag, typename Accept>
    requires IsFixyFn<F>
[[nodiscard]] constexpr std::expected<std::monostate,
                                       ::crucible::fixy::WireGradeError>
wire_decode_v2(std::span<const std::uint8_t> buf) noexcept
{
    if (buf.size() < kStanceHeaderSize) {
        return std::unexpected(
            ::crucible::fixy::WireGradeError::BufferTooSmall);
    }
    const std::uint16_t artifact_sid =
        ::crucible::fixy::detail::read_u16(buf, 0);
    const std::uint16_t artifact_sv  =
        ::crucible::fixy::detail::read_u16(buf, 2);

    // stance_id check — must match the consumer's declared tag.
    if (artifact_sid != ::crucible::fixy::stance::stance_id_of_v<Tag>) {
        return std::unexpected(
            ::crucible::fixy::WireGradeError::StanceIdMismatch);
    }
    // accept_versions window check.
    if (!::crucible::fixy::stance::version_in_window<Accept>(artifact_sv)) {
        return std::unexpected(
            ::crucible::fixy::WireGradeError::StanceVersionUnsupported);
    }
    // Delegate to V1 decoder for the body.
    auto sub = buf.subspan(kStanceHeaderSize);
    return ::crucible::fixy::wire_decode<F>(sub);
}

// ═════════════════════════════════════════════════════════════════════
// ── wire_decode_v1_under_v2_consumer<F, Tag> ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Legacy artifact load.  Only succeeds if
// `accept_legacy_stanceless<Tag> = true`; otherwise fails with
// `StanceVersionMissing`.

template <typename F, typename Tag>
    requires IsFixyFn<F>
[[nodiscard]] constexpr std::expected<std::monostate,
                                       ::crucible::fixy::WireGradeError>
wire_decode_v1_under_v2_consumer(std::span<const std::uint8_t> buf) noexcept
{
    if constexpr (
        ::crucible::fixy::stance::accept_legacy_stanceless_v<Tag>)
    {
        return ::crucible::fixy::wire_decode<F>(buf);
    } else {
        return std::unexpected(
            ::crucible::fixy::WireGradeError::StanceVersionMissing);
    }
}

}  // namespace crucible::fixy::stance
