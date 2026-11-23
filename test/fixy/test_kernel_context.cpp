// The Kernel context (fixy::Kernel, foundation/effects/Ctx.h): the context of
// a compute kernel.  Its row is empty, so a kernel allocates nothing, does no
// IO, does not block and reads no source of nondeterminism.  Its type states
// the FP mode of the register of its thread, and only a live FP scope of that
// mode on that thread mints it.
//
// A kernel takes the token of one call, which only run_kernel makes after the
// check of the context, so no call of a kernel skips the check.  The test
// kernel takes the token, a View of its input, the ViewMut of the frame of
// its output and a strided view of a bias row that broadcasts to each row.
// It runs under the IEEE mode and under the flush mode on one thread, and
// each run gives the bits of its mode.
//
// Two runs die: a context that a body takes to another thread, and a context
// that a function returns while its scope ends.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

#include "../thread_bound_probe.h"

#include <cstddef>
#include <cstdint>

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;
namespace eff = ::foundation::effects;

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;

struct KernelBrand {};

// The brand of the frame of the output in the checks of the type of the
// kernel.
struct OutputBrand {};

template <core::FpMode Mode>
using KernelCtx = ::fixy::Kernel<KernelBrand, Mode>;

template <core::FpMode Mode>
using KernelCall = ::fixy::KernelCall<KernelBrand, Mode>;

constexpr std::size_t kSide = 2;
constexpr std::size_t kElements = kSide * kSide;

// The bits of 1e-30f * 1e-10f with round to nearest and no flush: a
// subnormal far from the bounds of the subnormal range.
constexpr std::uint32_t kSubnormalProductBits = 0x000116C2u;
constexpr std::uint32_t kOneBits = 0x3F800000u;

// The dense square of a run of four elements.
template <class Element>
[[nodiscard]] ::fixy::StridedView<Element, 2> square_of(::fixy::View<Element, kElements> run) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{kSide, kSide};
    ::fixy::FixedArray<std::size_t, 2> const strides{kSide, 1};
    return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("the square fits its run");
}

// The unique dense square of the run of four elements of a frame.
template <class Element, class Brand>
[[nodiscard]] ::fixy::StridedViewMut<Element, 2, Brand>
square_of(::fixy::ViewMut<Element, Brand, kElements> run) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{kSide, kSide};
    ::fixy::FixedArray<std::size_t, 2> const strides{kSide, 1};
    return ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
        .expect("the square fits its run");
}

// The test kernel: out = in * scale + bias, over the square.  The walk adds
// in the row-major order of the square on each target.
template <core::FpMode Mode, class Brand>
[[gnu::noipa]] void scale_and_add(KernelCall<Mode> const&, ::fixy::View<float const, kElements> input,
                                  ::fixy::ViewMut<float, Brand, kElements> output,
                                  ::fixy::StridedView<float const, 2> bias, float scale) noexcept {
    ::fixy::StridedViewMut<float, 2, Brand> target_square = square_of(std::move(output));
    bool const is_walked = ::fixy::walk([scale](float& target, float const& value,
                                                float const& offset) noexcept { target = value * scale + offset; },
                                        target_square.view(), square_of(input), bias)
                               .is_ok();
    if (!is_walked) ::fixy::fatal("the three views of the kernel have one shape");
}

// The bits of the four results of one run of the kernel under Mode.
template <core::FpMode Mode>
[[nodiscard]] ::fixy::FixedArray<std::uint32_t, kElements> run_kernel_under() noexcept {
    os::FpScope<Mode> const scope = os::mint_fp_scope<Mode>();
    KernelCtx<Mode> const ctx = ::fixy::mint_kernel_context<KernelBrand>(scope);
    ::fixy::FixedArray<float, kElements> const input{1e-30F, 2.0F, 1e-30F, 3.0F};
    ::fixy::FixedArray<float, 2> const bias_row{0.0F, 1.0F};
    ::fixy::FixedArray<std::size_t, 1> const row_size{kSide};
    ::fixy::FixedArray<std::size_t, 1> const row_stride{1};
    ::fixy::FixedArray<std::size_t, 2> const square_sizes{kSide, kSide};
    ::fixy::StridedView<float const, 2> const bias =
        ::fixy::mint_strided_view(::fixy::View<float const, 2>{bias_row.view()}, row_size.view(), row_stride.view(), 0)
            .expect("one row fits")
            .broadcast_to(square_sizes.view())
            .expect("a row broadcasts to a square");
    ::fixy::FixedArray<float, kElements> output{};
    output.with_mut([&ctx, &input, &bias](auto run) noexcept {
        ::fixy::run_kernel(ctx, scale_and_add<Mode, typename decltype(run)::brand_type>,
                           ::fixy::View<float const, kElements>{input.view()}, std::move(run), bias, 1e-10F);
    });
    ::fixy::FixedArray<std::uint32_t, kElements> bits{};
    for (::fixy::Index<kElements> const index : output.indices()) {
        std::uint32_t const word = ::fixy::bit_cast<std::uint32_t>(output.at(index));
        bits.with_element(index, [word](std::uint32_t& held) noexcept { held = word; });
    }
    return bits;
}

// The type of the context: the empty row, no Nondet, the mode, one thread.
static_assert(eff::IsExecCtx<KernelCtx<core::kFpModeIeee>>);
static_assert(std::is_same_v<typename KernelCtx<core::kFpModeIeee>::row_type, eff::Row<>>);
static_assert(!eff::CtxOwnsCapability<KernelCtx<core::kFpModeIeee>, eff::Effect::Nondet>);
static_assert(
    !eff::CtxOwnsAnyOf<KernelCtx<core::kFpModeIeee>, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>);
static_assert(KernelCtx<core::kFpModeFlushToZero>::cap_type::fp_mode == core::kFpModeFlushToZero);
static_assert(!std::is_same_v<KernelCtx<core::kFpModeIeee>, KernelCtx<core::kFpModeFlushToZero>>,
              "two modes are two types, so a kernel of one mode takes no context of the other");
static_assert(!std::is_copy_constructible_v<KernelCtx<core::kFpModeIeee>>
                  && !std::is_move_constructible_v<KernelCtx<core::kFpModeIeee>>,
              "a kernel context has no copy and no move, so a by-value capture takes it to no other thread");

// A kernel takes the token of one call, and only run_kernel makes a token.
// So a direct call of a kernel with its context does not compile, and no
// call skips the check of run_kernel.
static_assert(
    ::fixy::IsKernelOf<decltype(&scale_and_add<core::kFpModeIeee, OutputBrand>), KernelBrand, core::kFpModeIeee,
                       ::fixy::View<float const, kElements>, ::fixy::ViewMut<float, OutputBrand, kElements>,
                       ::fixy::StridedView<float const, 2>, float>);
static_assert(
    !std::is_invocable_v<decltype(&scale_and_add<core::kFpModeIeee, OutputBrand>), KernelCtx<core::kFpModeIeee> const&,
                         ::fixy::View<float const, kElements>, ::fixy::ViewMut<float, OutputBrand, kElements>,
                         ::fixy::StridedView<float const, 2>, float>,
    "a direct call of a kernel with its context skips the check of run_kernel");
static_assert(!std::is_constructible_v<KernelCall<core::kFpModeIeee>, KernelCtx<core::kFpModeIeee> const&>);
static_assert(!std::is_copy_constructible_v<KernelCall<core::kFpModeIeee>>
                  && !std::is_move_constructible_v<KernelCall<core::kFpModeIeee>>,
              "a token has no copy and no move, so it does not live past its call");

// The kernel gives the bits of each mode, on one thread, one run after the
// other.
void test_the_kernel_gives_the_bits_of_each_mode(TestRunnerCtx const& test) {
    ::fixy::FixedArray<std::uint32_t, kElements> const ieee = run_kernel_under<core::kFpModeIeee>();
    ::fixy::FixedArray<std::uint32_t, kElements> const flushed = run_kernel_under<core::kFpModeFlushToZero>();
    ::fixy::FixedArray<std::uint32_t, kElements> const ieee_again = run_kernel_under<core::kFpModeIeee>();
    CRUCIBLE_TEST_ASSERT(test, ieee.at(::fixy::index<0>()) == kSubnormalProductBits);
    CRUCIBLE_TEST_ASSERT(test, ieee.at(::fixy::index<1>()) == kOneBits);
    CRUCIBLE_TEST_ASSERT(test, ieee.at(::fixy::index<2>()) == kSubnormalProductBits);
    CRUCIBLE_TEST_ASSERT(test, ieee.at(::fixy::index<3>()) == kOneBits);
    CRUCIBLE_TEST_ASSERT(test, flushed.at(::fixy::index<0>()) == 0u);
    CRUCIBLE_TEST_ASSERT(test, flushed.at(::fixy::index<1>()) == kOneBits);
    CRUCIBLE_TEST_ASSERT(test, flushed.at(::fixy::index<2>()) == 0u);
    CRUCIBLE_TEST_ASSERT(test, flushed.at(::fixy::index<3>()) == kOneBits);
    for (::fixy::Index<kElements> const index : ieee.indices()) {
        CRUCIBLE_TEST_ASSERT(test, ieee.at(index) == ieee_again.at(index));
    }
    ::fixy::check::pass(test, "the kernel gives the bits of the IEEE mode and of the flush mode on one thread\n");
}

// The token of a call gives the context that run_kernel checked: the epoch of
// its scope is the epoch of the scope of the mint.
void test_the_token_gives_the_checked_context(TestRunnerCtx const& test) {
    os::FpScope<core::kFpModeIeee> const scope = os::mint_fp_scope<core::kFpModeIeee>();
    KernelCtx<core::kFpModeIeee> const ctx = ::fixy::mint_kernel_context<KernelBrand>(scope);
    std::uint64_t seen_epoch = 0;
    ::fixy::run_kernel(ctx, [&seen_epoch](KernelCall<core::kFpModeIeee> const& call) noexcept {
        seen_epoch = call.ctx().cap().fp_scope_epoch();
    });
    CRUCIBLE_TEST_ASSERT(test, seen_epoch == scope.epoch());
    ::fixy::check::pass(test, "the token of a call gives the context that run_kernel checked\n");
}

// A context that reaches another thread names a scope that the register of
// that thread does not hold, so the run of the kernel there ends the process.
// The thread doors refuse a body that captures a context, so the probe gives
// the other thread the address of the context as an integer
// (test/thread_bound_probe.h).
void a_context_on_another_thread() {
    os::FpScope<core::kFpModeFlushToZero> const scope = os::mint_fp_scope<core::kFpModeFlushToZero>();
    KernelCtx<core::kFpModeFlushToZero> const ctx = ::fixy::mint_kernel_context<KernelBrand>(scope);
    ::fixy::BgLoadCtx const bg{eff::testing::bg()};
    ::fixy::Atomic<std::uint64_t> address{::crucible::test::address_of_thread_bound(ctx)};
    auto body = [&address]() noexcept {
        auto const& carried =
            ::crucible::test::thread_bound_at<KernelCtx<core::kFpModeFlushToZero> const>(address.load_acquire());
        ::fixy::run_kernel(carried, [](KernelCall<core::kFpModeFlushToZero> const&) noexcept {});
    };
    static_cast<void>(::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body)));
}

// A function can return a context by prvalue while its scope ends.  The
// epoch of that scope is no longer the epoch of the thread, so the run of the
// kernel ends the process.
[[nodiscard]] KernelCtx<core::kFpModeFlushToZero> context_past_its_scope() noexcept {
    os::FpScope<core::kFpModeFlushToZero> const scope = os::mint_fp_scope<core::kFpModeFlushToZero>();
    return ::fixy::mint_kernel_context<KernelBrand>(scope);
}

void a_context_past_its_scope() {
    KernelCtx<core::kFpModeFlushToZero> const ctx = context_past_its_scope();
    ::fixy::run_kernel(ctx, [](KernelCall<core::kFpModeFlushToZero> const&) noexcept {});
}

// An inner scope of another mode changes the register, so a context of the
// outer scope does not run while the inner scope lives.
void a_context_under_an_inner_scope() {
    os::FpScope<core::kFpModeFlushToZero> const outer = os::mint_fp_scope<core::kFpModeFlushToZero>();
    KernelCtx<core::kFpModeFlushToZero> const ctx = ::fixy::mint_kernel_context<KernelBrand>(outer);
    os::FpScope<core::kFpModeIeee> const inner = os::mint_fp_scope<core::kFpModeIeee>();
    ::fixy::run_kernel(ctx, [](KernelCall<core::kFpModeFlushToZero> const&) noexcept {});
}

// A raw write of the register under a live scope leaves the epoch of the
// scope innermost, so the check reads the mode of the register itself.
void a_context_after_a_raw_write() {
    os::FpScope<core::kFpModeFlushToZero> const scope = os::mint_fp_scope<core::kFpModeFlushToZero>();
    KernelCtx<core::kFpModeFlushToZero> const ctx = ::fixy::mint_kernel_context<KernelBrand>(scope);
    os::write_fp_control(os::FpControlWord::read().with_mode(core::kFpModeIeee));
    ::fixy::run_kernel(ctx, [](KernelCall<core::kFpModeFlushToZero> const&) noexcept {});
}

void test_a_context_runs_only_under_its_scope(TestRunnerCtx const& test) {
    expect_dies(test, a_context_on_another_thread, ::fixy::TextView{"fatal invariant failed: ctx.cap().thread() == "});
    expect_dies(test, a_context_past_its_scope,
                ::fixy::TextView{"fatal invariant failed: ::foundation::os::is_innermost_fp_scope"});
    expect_dies(test, a_context_under_an_inner_scope,
                ::fixy::TextView{"fatal invariant failed: ::foundation::os::is_innermost_fp_scope"});
    expect_dies(test, a_context_after_a_raw_write,
                ::fixy::TextView{"fatal invariant failed: ::foundation::os::FpControlWord::read().mode() == Mode"});
    ::fixy::check::pass(test, "a kernel context runs only on its thread, under its own live scope\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{eff::testing::test()};
    test_the_kernel_gives_the_bits_of_each_mode(test);
    test_the_token_gives_the_checked_context(test);
    test_a_context_runs_only_under_its_scope(test);
    return 0;
}
