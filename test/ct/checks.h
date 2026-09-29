#pragma once

// The taint checks of the constant-time primitives.  `Ct` names the
// primitives through static member functions, so a check does not spell
// the namespace of fixy::ct.  Each check marks its operands secret, runs
// the primitive inline and out of line, and makes only the result public.

#include "taint.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

namespace ct_taint {

// Operands pass through a volatile slot, so the optimizer cannot fold a
// primitive into a constant and hide the code under test.
template <typename T>
[[nodiscard]] T opaque(T value) noexcept {
    volatile T slot = value;
    return slot;
}

template <typename T>
constexpr std::array<T, 6> probe_values() noexcept {
    return {T{0},
            T{1},
            T{2},
            static_cast<T>(~T{0}),
            static_cast<T>(static_cast<T>(~T{0}) >> 1),
            static_cast<T>(static_cast<T>(static_cast<T>(~T{0}) >> 1) + T{1})};
}

// The out-of-line forms: a call the optimizer does not inline sees the
// primitive as its own function, as a caller in another translation unit
// does.
template <typename Ct, typename T>
[[gnu::noinline]] T select_outline(T bit01, T a, T b) noexcept {
    return Ct::select(bit01, a, b);
}
template <typename Ct, typename T>
[[gnu::noinline]] T less_outline(T a, T b) noexcept {
    return Ct::less(a, b);
}
template <typename Ct, typename T>
[[gnu::noinline]] T is_zero_outline(T x) noexcept {
    return Ct::is_zero(x);
}
template <typename Ct, typename T>
[[gnu::noinline]] void cswap_outline(T cond01, T& a, T& b) noexcept {
    Ct::cswap(cond01, a, b);
}
template <typename Ct>
[[gnu::noinline]] bool eq_outline(std::span<const std::byte> a, std::span<const std::byte> b) noexcept {
    return Ct::eq(a, b);
}

template <typename Ct, typename T>
void check_scalar_width(Tally& tally) noexcept {
    for (T const a_value : probe_values<T>()) {
        for (T const b_value : probe_values<T>()) {
            for (T const bit_value : {T{0}, T{1}}) {
                T a = opaque(a_value);
                T b = opaque(b_value);
                T bit = opaque(bit_value);
                make_secret(a);
                make_secret(b);
                make_secret(bit);

                T const mask = Ct::mask_from_bit(bit);
                T const chosen = Ct::select(bit, a, b);
                T const chosen_outline = select_outline<Ct>(bit, a, b);
                T const is_less = Ct::less(a, b);
                T const is_less_outline = less_outline<Ct>(a, b);
                T const zero_a = Ct::is_zero(a);
                T const zero_a_outline = is_zero_outline<Ct>(a);
                T left = a;
                T right = b;
                Ct::cswap(bit, left, right);
                T left_outline = a;
                T right_outline = b;
                cswap_outline<Ct>(bit, left_outline, right_outline);

                T const expected_mask = bit_value == T{1} ? static_cast<T>(~T{0}) : T{0};
                T const expected_chosen = bit_value == T{1} ? a_value : b_value;
                T const expected_less = a_value < b_value ? T{1} : T{0};
                T const expected_zero = a_value == T{0} ? T{1} : T{0};
                T const expected_left = bit_value == T{1} ? b_value : a_value;
                T const expected_right = bit_value == T{1} ? a_value : b_value;

                tally.expect(make_public(mask) == expected_mask, "mask_from_bit");
                tally.expect(make_public(chosen) == expected_chosen, "select");
                tally.expect(make_public(chosen_outline) == expected_chosen, "select, out of line");
                tally.expect(make_public(is_less) == expected_less, "less");
                tally.expect(make_public(is_less_outline) == expected_less, "less, out of line");
                tally.expect(make_public(zero_a) == expected_zero, "is_zero");
                tally.expect(make_public(zero_a_outline) == expected_zero, "is_zero, out of line");
                tally.expect(make_public(left) == expected_left && make_public(right) == expected_right, "cswap");
                tally.expect(make_public(left_outline) == expected_left && make_public(right_outline) == expected_right,
                             "cswap, out of line");
            }
        }
    }
}

template <typename Ct>
void check_scalars(Tally& tally) noexcept {
    check_scalar_width<Ct, std::uint8_t>(tally);
    check_scalar_width<Ct, std::uint16_t>(tally);
    check_scalar_width<Ct, std::uint32_t>(tally);
    check_scalar_width<Ct, std::uint64_t>(tally);
}

// Two byte buffers that differ at `diff_at`, or nowhere when diff_at is
// the length.  The contents are secret.  The length is public.
template <typename Ct, std::size_t N>
void check_eq_length(Tally& tally) noexcept {
    for (std::size_t diff_at = 0; diff_at <= N; ++diff_at) {
        std::array<std::byte, N> a{};
        std::array<std::byte, N> b{};
        for (std::size_t i = 0; i < N; ++i) {
            a[i] = static_cast<std::byte>(opaque(static_cast<unsigned>(i * 37u + 11u)));
            b[i] = a[i];
        }
        if (diff_at < N) b[diff_at] ^= std::byte{0x40};
        make_secret_bytes(a.data(), a.size());
        make_secret_bytes(b.data(), b.size());

        bool const expected = diff_at == N;
        tally.expect(make_public(Ct::eq(std::span<const std::byte>{a}, std::span<const std::byte>{b})) == expected,
                     "eq");
        tally.expect(make_public(eq_outline<Ct>(a, b)) == expected, "eq, out of line");
        if constexpr (requires { Ct::template eq_static<N>(a, b); }) {
            tally.expect(make_public(Ct::template eq_static<N>(a, b)) == expected, "eq, static extent");
        }
    }
}

template <typename Ct>
void check_eq(Tally& tally) noexcept {
    check_eq_length<Ct, 1>(tally);
    check_eq_length<Ct, 7>(tally);
    check_eq_length<Ct, 16>(tally);
    check_eq_length<Ct, 32>(tally);
    check_eq_length<Ct, 64>(tally);
}

// The leaking controls.  Each branches or indexes on a secret on purpose.
// The branch calls a function on one arm, so the optimizer cannot turn it
// into a conditional move, which memcheck does not report.
[[gnu::noinline]] inline int control_branch_on_secret(std::uint32_t secret) noexcept {
    if (secret & 1u) {
        std::puts("control: the secret is odd");
        return 3;
    }
    return 5;
}

[[gnu::noinline]] inline int control_index_by_secret(std::uint32_t secret) noexcept {
    static std::array<std::uint8_t, 16> table{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    return table[secret & 15u];
}

// A control passes only when memcheck reported an error inside the
// leaking call.  A harness that sees nothing proves nothing, so the
// control fails in that case.
template <typename Leak>
int run_control(Leak&& leak) noexcept {
    std::uint32_t secret = opaque(7u);
    make_secret(secret);
    unsigned long const before = reported_errors();
    int const result = leak(secret);
    unsigned long const after = reported_errors();
    std::printf("control: result %d, %lu memcheck errors in the leaking call\n", make_public(result), after - before);
    if (after == before) {
        std::fputs("ct_taint: the control leaked a secret and memcheck reported nothing, so the harness is blind\n",
                   stderr);
        return 1;
    }
    return 0;
}

// The shared entry: a control mode when one is named, the checks
// otherwise.  `run_checks` is the list of checks of the test.
template <typename Checks>
int run(int argc, char** argv, Checks&& run_checks) noexcept {
    require_valgrind();
    if (argc == 2) {
        std::string_view const mode{argv[1]};
        if (mode == "--control-branch") return run_control(control_branch_on_secret);
        if (mode == "--control-index") return run_control(control_index_by_secret);
        std::fprintf(stderr, "ct_taint: unknown mode %s\n", argv[1]);
        return 2;
    }
    Tally tally;
    run_checks(tally);
    std::printf("ct_taint: %d wrong results, %lu memcheck errors\n", tally.failures, reported_errors());
    return tally.failures == 0 ? 0 : 1;
}

}  // namespace ct_taint
