// Tests of the dispatch door of fixy/concurrent/Dispatch.h: the keys of
// dispatch brands, the window of one dispatch on a thread, and the shape of
// the record kernel of the vessel, whose redispatch records each nested
// operation before the operation dispatches to its owner.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/Dispatch.h>
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <type_traits>
#include <utility>

namespace {

namespace fc = ::foundation::core;
namespace fd = ::fixy::dispatch;
namespace eff = ::foundation::effects;

struct[[= fc::dispatch_brand{}]] LogBrand final {};
struct[[= fc::dispatch_brand{}]] RingBrand final {};
struct PlainBrand final {};

// The stand-in of the Vigil: an owner of keyed owners of two dispatch brands,
// which a dispatch body reaches through a borrow.
struct Recorder {
    fc::FixedArray<int, 4, LogBrand> log{};
    fc::FixedArray<int, 4, RingBrand> ring{};
};

using DispatchCtx = eff::ExecCtx<eff::ctx_cap::BrandedFg<Recorder>, eff::Row<>>;

constexpr fc::TextView kNested{"fixy::dispatch: a dispatch opened inside a dispatch window of the same thread"};

// A key of a dispatch brand does not move, and it is no linear class.  A key of
// another brand moves, and it is linear.  Neither is trivially copyable, so
// std::bit_cast builds neither.
static_assert(!std::is_move_constructible_v<fc::LoanKey<LogBrand>>);
static_assert(!::foundation::linear::is_linear_v<fc::LoanKey<LogBrand>>);
static_assert(!std::is_trivially_copyable_v<fc::LoanKey<LogBrand>>);
static_assert(std::is_nothrow_move_constructible_v<fc::LoanKey<PlainBrand>>);
static_assert(::foundation::linear::is_linear_v<fc::LoanKey<PlainBrand>>);
static_assert(fc::IsDispatchBrand<LogBrand> && !fc::IsDispatchBrand<PlainBrand>);

// Only a context with a branded foreground source opens a dispatch.
static_assert(fd::CtxOpensDispatch<DispatchCtx>);
static_assert(!fd::CtxOpensDispatch<::fixy::BgLoadCtx>);

// True when run_dispatch takes a body of the type that the caller names as an
// lvalue.
template <typename Body>
concept DispatchesAnLvalueBody = requires(DispatchCtx const& ctx, Body& body) { fd::run_dispatch(ctx, body); };

// The body borrows the owner of its brands, which the borrow walk of the
// doors that start a thread would refuse, and opens a frame of each keyed
// owner with its key.  The door gives the value of the body.
void test_body_borrows_the_owners_of_its_brands(DispatchCtx const& ctx) {
    Recorder recorder{};
    auto body = [&recorder](DispatchCtx const& /*dispatch_ctx*/, fc::LoanKey<LogBrand>& log_key,
                            fc::LoanKey<RingBrand>& ring_key) noexcept {
        recorder.log.with_mut(log_key, [](auto run) noexcept { run.view().at(fc::index<0>()) = 3; });
        recorder.ring.with_mut(ring_key, [](auto run) noexcept { run.view().at(fc::index<0>()) = 4; });
        return recorder.log.at(fc::index<0>()) + recorder.ring.at(fc::index<0>());
    };
    static_assert(!DispatchesAnLvalueBody<decltype(body)>);
    int const sum = fd::run_dispatch(ctx, std::move(body));
    CRUCIBLE_FATAL_INVARIANT(sum == 7);
}

// The dispatch of one operation to the owner, in a window of its own.
void dispatch_one_operation(DispatchCtx const& ctx, Recorder& recorder) noexcept {
    fd::run_dispatch(ctx, [&recorder](DispatchCtx const& /*dispatch_ctx*/, fc::LoanKey<LogBrand>& key) noexcept {
        recorder.log.with_mut(key, [](auto run) noexcept { run.view().at(fc::index<1>()) += 1; });
    });
}

// The shape of call_recording of vessel/torch/record_kernel.h: the redispatch
// of an operation runs a nested operation, which records and dispatches in a
// window of its own, and the outer operation dispatches after the redispatch
// returns.  The two windows follow each other on one thread, so neither is
// nested.
void test_a_record_during_the_redispatch_opens_its_own_window(DispatchCtx const& ctx) {
    Recorder recorder{};
    dispatch_one_operation(ctx, recorder);
    dispatch_one_operation(ctx, recorder);
    CRUCIBLE_FATAL_INVARIANT(recorder.log.at(fc::index<1>()) == 2);
}

// A dispatch inside the window of the same thread ends the process: from the
// body itself, and from a frame that the body opened.
void test_a_nested_dispatch_ends_the_process(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            DispatchCtx const ctx = eff::testing::foreground<Recorder>();
            Recorder recorder{};
            fd::run_dispatch(ctx, [&ctx, &recorder](DispatchCtx const& /*dispatch_ctx*/,
                                                    fc::LoanKey<RingBrand>& /*ring_key*/) noexcept {
                dispatch_one_operation(ctx, recorder);
            });
        },
        kNested);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            DispatchCtx const ctx = eff::testing::foreground<Recorder>();
            Recorder recorder{};
            fd::run_dispatch(
                ctx, [&ctx, &recorder](DispatchCtx const& /*dispatch_ctx*/, fc::LoanKey<RingBrand>& ring_key) noexcept {
                    recorder.ring.with_mut(ring_key, [&ctx, &recorder](auto run) noexcept {
                        dispatch_one_operation(ctx, recorder);
                        run.view().at(fc::index<0>()) = 1;
                    });
                });
        },
        kNested);
    ::fixy::check::pass(test, "  a nested dispatch ends the process\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    DispatchCtx const ctx = eff::testing::foreground<Recorder>();
    test_body_borrows_the_owners_of_its_brands(ctx);
    test_a_record_during_the_redispatch_opens_its_own_window(ctx);
    test_a_nested_dispatch_ends_the_process(test);
    return 0;
}
