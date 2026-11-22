#pragma once

// The FP control door of the base.  It is the only code of the tree that
// reads or writes the floating-point control register of a thread: MXCSR on
// x86_64, FPCR on aarch64.
//
// Register
//   x86_64, MXCSR (32 bits):
//     bits 0-5    the sticky exception flags (IE DE ZE OE UE PE)
//     bit 6       DAZ, a subnormal input reads as zero
//     bits 7-12   the exception masks, each set at process start
//     bits 13-14  RC, the rounding: 00 nearest, 01 down, 10 up, 11 toward zero
//     bit 15      FTZ, a subnormal result is zero
//   aarch64, FPCR (64 bits, the high half is reserved and zero):
//     bit 19      FZ16, the flush of half precision
//     bits 22-23  RMode: 00 nearest, 01 up, 10 down, 11 toward zero
//     bit 24      FZ, the flush of subnormal inputs and results of single and
//                 double precision, while FPCR.AH is zero
//   The exception flags of aarch64 are in FPSR, not in FPCR.
//
// Encoding
//   FpControlWord::with_mode puts a mode into a word and keeps each other
//   bit, apart from two.  On x86_64 it clears the sticky flags.  On aarch64
//   it clears FZ16, because a half result is never flushed on x86_64 either
//   (the F16C conversions ignore FTZ and DAZ).  On aarch64 without FEAT_AFP
//   one bit, FZ, flushes the inputs and the results.  So a mode whose two
//   flush settings differ has no word there, and is_encodable_fp_mode is
//   false for it.  FpControlWord::mode reads the three settings back, so a
//   compare of two modes ignores the sticky flags and each other bit.
//
// Scope
//   FpScope<Mode> sets the register of the calling thread to Mode for its
//   life, and it writes back the word of before at its end.  Each scope takes
//   a new epoch, which is unique in the process, and the thread keeps the
//   epoch of its innermost scope in a thread-local slot.  So the one compare
//   is_innermost_fp_scope(epoch) tells that the calling thread is the thread
//   of the scope, that the scope is live, and that no inner scope of another
//   mode is open.  A kernel context keeps the epoch of the scope that it came
//   from (foundation/effects/Ctx.h).
//
// Cost
//   A write of MXCSR can serialize the FP pipeline, so a scope writes the
//   register only when the mode of the register differs from its mode.
//   bench/bench_fp_control.cpp measures one read and one write.
//
// The register is a state of the thread.  GCC does not model it: with no
// -frounding-math, GCC can move an FP operation across a write.  A caller
// that must order an operation after a write calls a function that the
// optimizer cannot see into.
//
// This header is in the layer F0_doors of utils/rule-table.txt.  It needs no
// system header: each access is a builtin or one instruction.

#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/FpMode.h>

#include <cstdint>

namespace foundation::os {

namespace detail {

#if defined(__x86_64__)
inline constexpr std::uint64_t kMxcsrFlags = 0x3F;
inline constexpr std::uint64_t kMxcsrDaz = std::uint64_t{1} << 6;
inline constexpr std::uint64_t kMxcsrRoundingShift = 13;
inline constexpr std::uint64_t kMxcsrRounding = std::uint64_t{3} << kMxcsrRoundingShift;
inline constexpr std::uint64_t kMxcsrFtz = std::uint64_t{1} << 15;
#elif defined(__aarch64__)
inline constexpr std::uint64_t kFpcrFz16 = std::uint64_t{1} << 19;
inline constexpr std::uint64_t kFpcrRoundingShift = 22;
inline constexpr std::uint64_t kFpcrRounding = std::uint64_t{3} << kFpcrRoundingShift;
inline constexpr std::uint64_t kFpcrFz = std::uint64_t{1} << 24;
#else
#error "foundation/os/FpControl.h: the FP control door covers x86_64 and aarch64 only."
#endif

// The two bits of the rounding field for a mode, and the mode of two bits.
// The two targets order the directions differently.
[[nodiscard]] constexpr std::uint64_t rounding_bits_of(core::RoundingMode rounding) noexcept {
#if defined(__x86_64__)
    switch (rounding) {
        case core::RoundingMode::RN:
            return 0;
        case core::RoundingMode::RM:
            return 1;
        case core::RoundingMode::RP:
            return 2;
        case core::RoundingMode::RZ:
            return 3;
        default:
            return 0;
    }
#else
    switch (rounding) {
        case core::RoundingMode::RN:
            return 0;
        case core::RoundingMode::RP:
            return 1;
        case core::RoundingMode::RM:
            return 2;
        case core::RoundingMode::RZ:
            return 3;
        default:
            return 0;
    }
#endif
}

[[nodiscard]] constexpr core::RoundingMode rounding_of_bits(std::uint64_t bits) noexcept {
#if defined(__x86_64__)
    constexpr core::RoundingMode order[] = {core::RoundingMode::RN, core::RoundingMode::RM, core::RoundingMode::RP,
                                            core::RoundingMode::RZ};
#else
    constexpr core::RoundingMode order[] = {core::RoundingMode::RN, core::RoundingMode::RP, core::RoundingMode::RM,
                                            core::RoundingMode::RZ};
#endif
    return order[bits & 3];
}

}  // namespace detail

// True when the register of this target has a word for the mode.  x86_64
// has one for each mode.  aarch64 without FEAT_AFP has one bit for the two
// flush settings, so they must agree there.
[[nodiscard]] constexpr bool is_encodable_fp_mode(core::FpMode mode) noexcept {
#if defined(__x86_64__)
    static_cast<void>(mode);
    return true;
#else
    return (mode.ftz == core::FpFtz::FlushToZero) == (mode.denormal_input == core::FpDenormalInput::DenormalsAreZero);
#endif
}

template <core::FpMode Mode>
concept EncodableFpMode = is_encodable_fp_mode(Mode);

// The word of the FP control register of one thread.  A word comes from a
// read of the register or from a word and a mode, never from free bits.
// The copy is user-provided, so the word is not trivially copyable, and
// std::bit_cast does not build one from free bits.
class FpControlWord {
public:
    constexpr FpControlWord(FpControlWord const& other) noexcept : bits_{other.bits_} {}
    constexpr FpControlWord& operator=(FpControlWord const& other) noexcept {
        bits_ = other.bits_;
        return *this;
    }
    ~FpControlWord() = default;

    // The word of the calling thread.
    [[nodiscard]] CRUCIBLE_INLINE static FpControlWord read() noexcept {
#if defined(__x86_64__)
        return FpControlWord{std::uint64_t{__builtin_ia32_stmxcsr()}};
#else
        std::uint64_t value = 0;
        asm volatile("mrs %0, fpcr" : "=r"(value));
        return FpControlWord{value};
#endif
    }

    // This word with the three settings of MODE.  The register of this
    // target has a word for the mode (is_encodable_fp_mode).
    [[nodiscard]] constexpr FpControlWord with_mode(core::FpMode mode) const noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_encodable_fp_mode(mode));
#if defined(__x86_64__)
        std::uint64_t bits =
            bits_ & ~(detail::kMxcsrFlags | detail::kMxcsrDaz | detail::kMxcsrRounding | detail::kMxcsrFtz);
        bits |= detail::rounding_bits_of(mode.rounding) << detail::kMxcsrRoundingShift;
        if (mode.ftz == core::FpFtz::FlushToZero) bits |= detail::kMxcsrFtz;
        if (mode.denormal_input == core::FpDenormalInput::DenormalsAreZero) bits |= detail::kMxcsrDaz;
#else
        std::uint64_t bits = bits_ & ~(detail::kFpcrFz16 | detail::kFpcrRounding | detail::kFpcrFz);
        bits |= detail::rounding_bits_of(mode.rounding) << detail::kFpcrRoundingShift;
        if (mode.ftz == core::FpFtz::FlushToZero) bits |= detail::kFpcrFz;
#endif
        return FpControlWord{bits};
    }

    // The three settings of this word.
    [[nodiscard]] constexpr core::FpMode mode() const noexcept {
#if defined(__x86_64__)
        return core::FpMode{
            .rounding = detail::rounding_of_bits(bits_ >> detail::kMxcsrRoundingShift),
            .ftz = (bits_ & detail::kMxcsrFtz) != 0 ? core::FpFtz::FlushToZero : core::FpFtz::PreserveSubnormals,
            .denormal_input = (bits_ & detail::kMxcsrDaz) != 0 ? core::FpDenormalInput::DenormalsAreZero
                                                               : core::FpDenormalInput::HonorDenormals,
        };
#else
        bool const is_flushed = (bits_ & detail::kFpcrFz) != 0;
        return core::FpMode{
            .rounding = detail::rounding_of_bits(bits_ >> detail::kFpcrRoundingShift),
            .ftz = is_flushed ? core::FpFtz::FlushToZero : core::FpFtz::PreserveSubnormals,
            .denormal_input =
                is_flushed ? core::FpDenormalInput::DenormalsAreZero : core::FpDenormalInput::HonorDenormals,
        };
#endif
    }

    // The bits of the register, for a report.
    [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

    [[nodiscard]] friend constexpr bool operator==(FpControlWord, FpControlWord) noexcept = default;

private:
    explicit constexpr FpControlWord(std::uint64_t bits) noexcept : bits_{bits} {}

    std::uint64_t bits_ = 0;
};

// Writes the word to the register of the calling thread.
CRUCIBLE_INLINE void write_fp_control(FpControlWord word) noexcept {
#if defined(__x86_64__)
    __builtin_ia32_ldmxcsr(static_cast<unsigned>(word.bits()));
#else
    asm volatile("msr fpcr, %0" : : "r"(word.bits()) : "memory");
#endif
}

namespace detail {

// The epoch of the innermost FP scope of the calling thread, or zero when
// the thread holds no scope.  One slot for each thread of the process.  The
// initial-exec model makes each access one load relative to the thread
// pointer.
CRUCIBLE_PROCESS_WIDE
[[gnu::tls_model("initial-exec")]] inline constinit thread_local std::uint64_t fp_scope_epoch_of_thread = 0;

// The source of the epochs.  Each scope of the process takes the next
// value, so no two scopes of the process share an epoch, and zero names no
// scope.
CRUCIBLE_PROCESS_WIDE inline constinit core::Atomic<std::uint64_t> fp_scope_epochs{std::uint64_t{0}};

}  // namespace detail

// True when the scope of EPOCH is the innermost FP scope of the calling
// thread (the scope).
[[nodiscard]] CRUCIBLE_INLINE bool is_innermost_fp_scope(std::uint64_t epoch) noexcept {
    return epoch != 0 && detail::fp_scope_epoch_of_thread == epoch;
}

template <core::FpMode Mode>
    requires EncodableFpMode<Mode>
class FpScope;

template <core::FpMode Mode>
    requires EncodableFpMode<Mode>
[[nodiscard]] FpScope<Mode> mint_fp_scope() noexcept;

// The FP mode Mode on the calling thread, for the life of the scope (THE
// SCOPE).  A scope stays in the frame of its mint: it has no copy and no
// move, so the thread that opens it also ends it.  Scopes end in the
// reverse order of their start, and an end out of that order ends the
// process.
template <core::FpMode Mode>
    requires EncodableFpMode<Mode>
class [[nodiscard]] FpScope {
public:
    static constexpr core::FpMode mode = Mode;

    FpScope(const FpScope&) = delete("a scope restores the register of one thread, one time");
    FpScope(FpScope&&) = delete("a scope restores the register of the thread that opened it");
    FpScope& operator=(const FpScope&) = delete("a scope restores the register of one thread, one time");
    FpScope& operator=(FpScope&&) = delete("a scope restores the register of one thread, one time");

    ~FpScope() noexcept {
        CRUCIBLE_FATAL_INVARIANT(detail::fp_scope_epoch_of_thread == epoch_);
        detail::fp_scope_epoch_of_thread = previous_epoch_;
        if (previous_word_.mode() != Mode) write_fp_control(previous_word_);
    }

    // The epoch of this scope, unique in the process.
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }

private:
    FpScope() noexcept
        : previous_word_{FpControlWord::read()},
          previous_epoch_{detail::fp_scope_epoch_of_thread},
          epoch_{detail::fp_scope_epochs.fetch_add_acq_rel(1) + 1} {
        if (previous_word_.mode() != Mode) write_fp_control(previous_word_.with_mode(Mode));
        detail::fp_scope_epoch_of_thread = epoch_;
    }

    template <core::FpMode FriendMode>
        requires EncodableFpMode<FriendMode>
    friend FpScope<FriendMode> mint_fp_scope() noexcept;

    FpControlWord previous_word_;
    std::uint64_t previous_epoch_ = 0;
    std::uint64_t epoch_ = 0;
};

// Opens an FP scope of Mode on the calling thread.  The register then holds
// Mode until the scope ends.
template <core::FpMode Mode>
    requires EncodableFpMode<Mode>
[[nodiscard]] FpScope<Mode> mint_fp_scope() noexcept {
    return FpScope<Mode>{};
}

}  // namespace foundation::os
