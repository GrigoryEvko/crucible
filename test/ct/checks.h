#pragma once

// The taint checks of the constant-time primitives.  `Ct` names the
// primitives through static member functions, so a check does not spell
// the namespace of fixy::ct.  Each check marks its operands secret, runs
// the primitive inline and out of line, and makes only the result public.
//
// A Tally of the Check family counts the functional checks.  The taint
// result is separate: memcheck reports its own errors and --error-exitcode
// turns them into a failure.

#include "taint.h"

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Args.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace ct_taint {

inline constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// Operands pass through a volatile slot, so the optimizer cannot fold a
// primitive into a constant and hide the code under test.
template <typename T>
[[nodiscard]] T opaque(T value) noexcept {
    volatile T slot = value;
    return slot;
}

template <typename T>
constexpr ::fixy::FixedArray<T, 6> probe_values() noexcept {
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
[[gnu::noinline]] bool eq_outline(::fixy::View<std::byte const> a, ::fixy::View<std::byte const> b) noexcept {
    return Ct::eq(a, b);
}

template <typename Ct, typename T>
void check_scalar_width(::fixy::check::Tally& tally) noexcept {
    using enum ::fixy::check::CheckOutcome;
    constexpr ::fixy::FixedArray<T, 6> values = probe_values<T>();
    constexpr ::fixy::FixedArray<T, 2> bits{T{0}, T{1}};
    for (T const a_value : values) {
        for (T const b_value : values) {
            for (T const bit_value : bits) {
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

                tally.count(make_public(mask) == expected_mask ? held : failed, "mask_from_bit");
                tally.count(make_public(chosen) == expected_chosen ? held : failed, "select");
                tally.count(make_public(chosen_outline) == expected_chosen ? held : failed, "select, out of line");
                tally.count(make_public(is_less) == expected_less ? held : failed, "less");
                tally.count(make_public(is_less_outline) == expected_less ? held : failed, "less, out of line");
                tally.count(make_public(zero_a) == expected_zero ? held : failed, "is_zero");
                tally.count(make_public(zero_a_outline) == expected_zero ? held : failed, "is_zero, out of line");
                tally.count(make_public(left) == expected_left && make_public(right) == expected_right ? held : failed,
                            "cswap");
                tally.count(make_public(left_outline) == expected_left && make_public(right_outline) == expected_right
                                ? held
                                : failed,
                            "cswap, out of line");
            }
        }
    }
}

template <typename Ct>
void check_scalars(::fixy::check::Tally& tally) noexcept {
    check_scalar_width<Ct, std::uint8_t>(tally);
    check_scalar_width<Ct, std::uint16_t>(tally);
    check_scalar_width<Ct, std::uint32_t>(tally);
    check_scalar_width<Ct, std::uint64_t>(tally);
}

// Two byte buffers that differ at `diff_at`, or nowhere when diff_at is
// the length.  The contents are secret.  The length is public.  An array
// holds its bytes and no other byte, so make_secret marks exactly them.
template <typename Ct, std::size_t N>
void check_eq_length(::fixy::check::Tally& tally) noexcept {
    using enum ::fixy::check::CheckOutcome;
    using Bytes = ::fixy::FixedArray<std::byte, N>;
    static_assert(sizeof(Bytes) == N, "make_secret of an array marks the bytes of its elements and no other byte");
    for (std::size_t diff_at = 0; diff_at <= N; ++diff_at) {
        Bytes a{};
        Bytes b{};
        for (::fixy::Index<N> const index : a.indices()) {
            auto const value = static_cast<std::byte>(opaque(static_cast<unsigned>(index.raw() * 37u + 11u)));
            a.with_element(index, [value](std::byte& element) noexcept { element = value; });
            std::byte const copied = a.at(index);
            b.with_element(index, [copied](std::byte& element) noexcept { element = copied; });
        }
        ::fixy::Index<N>::check(diff_at).match(
            [&b](::fixy::Index<N> const at) noexcept {
                b.with_element(at, [](std::byte& element) noexcept {
                    element = static_cast<std::byte>(static_cast<unsigned char>(element) ^ 0x40u);
                });
            },
            [] noexcept {});
        make_secret(a);
        make_secret(b);

        bool const expected = diff_at == N;
        tally.count(make_public(Ct::eq(::fixy::View<std::byte const>{::fixy::bytes(a)},
                                       ::fixy::View<std::byte const>{::fixy::bytes(b)}))
                            == expected
                        ? held
                        : failed,
                    "eq");
        tally.count(make_public(eq_outline<Ct>(::fixy::bytes(a), ::fixy::bytes(b))) == expected ? held : failed,
                    "eq, out of line");
        if constexpr (requires { Ct::template eq_static<N>(::fixy::bytes(a), ::fixy::bytes(b)); }) {
            tally.count(make_public(Ct::template eq_static<N>(::fixy::bytes(a), ::fixy::bytes(b))) == expected ? held
                                                                                                               : failed,
                        "eq, static extent");
        }
    }
}

template <typename Ct>
void check_eq(::fixy::check::Tally& tally) noexcept {
    check_eq_length<Ct, 1>(tally);
    check_eq_length<Ct, 7>(tally);
    check_eq_length<Ct, 16>(tally);
    check_eq_length<Ct, 32>(tally);
    check_eq_length<Ct, 64>(tally);
}

// The leaking controls.  Each branches or indexes on a secret on purpose.
// The branch calls a function on one arm, so the optimizer cannot turn it
// into a conditional move, which memcheck does not report.  The index is
// below the count of the table by its mask, so its check folds away and the
// read is the only use of the secret.
[[gnu::noinline]] inline int control_branch_on_secret(std::uint32_t secret) noexcept {
    if (secret & 1u) {
        ::fixy::report(::fixy::Sink::Out, "control: the secret is odd\n");
        return 3;
    }
    return 5;
}

[[gnu::noinline]] inline int control_index_by_secret(std::uint32_t secret) noexcept {
    static constexpr ::fixy::FixedArray<std::uint8_t, 16> table{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    return table.at(::fixy::Index<16>::check(secret & 15u).expect("a mask of four bits gives an index below 16"));
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
    ::fixy::report(::fixy::Sink::Out, "control: result {}, {} memcheck errors in the leaking call\n",
                   make_public(result), after - before);
    if (after == before) {
        ::fixy::report(
            ::fixy::Sink::Err,
            "ct_taint: the control leaked a secret and memcheck reported nothing, so the harness is blind\n");
        return 1;
    }
    return 0;
}

// The shared entry: a control mode when one is named, the checks
// otherwise.  The argument door of fixy/os/Args.h gives the arguments of
// main.  `run_checks` is the list of checks of the test.  A run outside
// valgrind, or a run that asks a debuginfod server, ends with the code 2.
// A failed check ends the run at the close of the Tally.
template <typename Checks>
int run(::fixy::os::Args const arguments, Checks&& run_checks) noexcept {
    if (!is_under_valgrind()) {
        ::fixy::report(::fixy::Sink::Err,
                       "ct_taint: this test must run under valgrind --tool=memcheck; outside valgrind every "
                       "taint request is a no-op and the run proves nothing\n");
        return 2;
    }
    if (names_debuginfod_server()) {
        ::fixy::report(::fixy::Sink::Err,
                       "ct_taint: DEBUGINFOD_URLS names a server, so valgrind asked the network for debug information "
                       "at its start.  Unset DEBUGINFOD_URLS for this test.\n");
        return 2;
    }
    if (arguments.size() == 2) {
        ::fixy::TextView const mode = arguments.get(1).expect("a count of two holds a first argument").view();
        if (mode == ::fixy::TextView{"--control-branch"}) return run_control(control_branch_on_secret);
        if (mode == ::fixy::TextView{"--control-index"}) return run_control(control_index_by_secret);
        ::fixy::report(::fixy::Sink::Err, "ct_taint: unknown mode {}\n", mode);
        return 2;
    }
    ::fixy::check::Tally tally{kTest, "ct_taint"};
    run_checks(tally);
    std::move(tally).close();
    ::fixy::report(::fixy::Sink::Out, "ct_taint: 0 wrong results, {} memcheck errors\n", reported_errors());
    return 0;
}

}  // namespace ct_taint
