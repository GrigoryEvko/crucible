#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Op must also be associative, because a parallel fold merges partial
// accumulators in an unspecified grouping.  That obligation is on the caller:
// this predicate checks the call shape only.  Commutativity is not required —
// combine applies Op with the accumulator first.
template <typename Op, typename R>
concept is_reduction_op_v = std::is_invocable_v<Op const&, R const&, R const&>
                         && std::is_convertible_v<std::invoke_result_t<Op const&, R const&, R const&>, R>;

template <typename R, typename Op>
    requires is_reduction_op_v<Op, R>
class [[nodiscard]] reduce_into {
public:
    using accumulator_type = R;
    using reducer_type = Op;

    constexpr reduce_into(R init, Op op) noexcept(std::is_nothrow_move_constructible_v<R>
                                                  && std::is_nothrow_move_constructible_v<Op>)
        : acc_{std::move(init)}, op_{std::move(op)} {}

    reduce_into(reduce_into const&) = delete;
    reduce_into& operator=(reduce_into const&) = delete;

    constexpr reduce_into(reduce_into&&) = default;
    constexpr reduce_into& operator=(reduce_into&&) = default;

    ~reduce_into() = default;

    [[nodiscard]] constexpr R const& peek() const& noexcept { return acc_; }

    [[nodiscard]] constexpr R& peek_mut() & noexcept { return acc_; }

    [[nodiscard]] constexpr R consume() && noexcept(std::is_nothrow_move_constructible_v<R>) { return std::move(acc_); }

    [[nodiscard]] constexpr Op const& reducer() const& noexcept { return op_; }

    constexpr void combine(R const& partial) noexcept(noexcept(std::declval<Op const&>()(std::declval<R const&>(),
                                                                                         std::declval<R const&>()))) {
        acc_ = op_(acc_, partial);
    }

private:
    R acc_;
    Op op_;
};

namespace detail::reduce_into_self_test {

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
};

struct MaxOp {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a > b ? a : b; }
};

static_assert(is_reduction_op_v<PlusOp, int>);
static_assert(is_reduction_op_v<MaxOp, int>);

struct NotInvocable {};
static_assert(!is_reduction_op_v<NotInvocable, int>);

struct WrongArity {
    constexpr int operator()(int) const noexcept { return 0; }
};
static_assert(!is_reduction_op_v<WrongArity, int>);

struct WrongReturn {
    constexpr void operator()(int const&, int const&) const noexcept {}
};
static_assert(!is_reduction_op_v<WrongReturn, int>);

[[nodiscard]] consteval bool construct_and_peek() noexcept {
    reduce_into<int, PlusOp> r{0, PlusOp{}};
    return r.peek() == 0;
}
static_assert(construct_and_peek());

[[nodiscard]] consteval bool combine_folds() noexcept {
    reduce_into<int, PlusOp> r{0, PlusOp{}};
    r.combine(7);
    r.combine(35);
    return r.peek() == 42;
}
static_assert(combine_folds());

[[nodiscard]] consteval bool consume_extracts() noexcept {
    reduce_into<int, PlusOp> r{42, PlusOp{}};
    int v = std::move(r).consume();
    return v == 42;
}
static_assert(consume_extracts());

static_assert(!std::is_copy_constructible_v<reduce_into<int, PlusOp>>);
static_assert(!std::is_copy_assignable_v<reduce_into<int, PlusOp>>);
static_assert(std::is_move_constructible_v<reduce_into<int, PlusOp>>);
static_assert(std::is_move_assignable_v<reduce_into<int, PlusOp>>);

}  // namespace detail::reduce_into_self_test

inline bool reduce_into_smoke_test() noexcept {
    using namespace detail::reduce_into_self_test;

    volatile int const seed = 0;
    reduce_into<int, PlusOp> r{static_cast<int>(seed), PlusOp{}};
    r.combine(7);
    r.combine(35);

    bool ok = (r.peek() == 42);

    reduce_into<int, MaxOp> rmax{0, MaxOp{}};
    rmax.combine(7);
    rmax.combine(3);
    rmax.combine(99);
    rmax.combine(42);
    ok = ok && (rmax.peek() == 99);

    return ok;
}

}  // namespace crucible::safety
