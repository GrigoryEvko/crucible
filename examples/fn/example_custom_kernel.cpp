// example_custom_kernel: a user kernel bound to the background thread.
//
// A user binds a dense GEMM kernel, C = A @ B, (M x K) @ (K x N), to the
// dispatch path of Crucible.  The kernel runs on the background thread
// over FP32 matrices.  The binding records the effects, the mutation, the
// reentrancy and the provenance of the kernel in its type, and it costs
// nothing at run time.
//
// A pack names an atom only for an axis where the binding says more than
// the strict pole.  Each axis that the pack does not name takes its
// strict pole: linear usage, the empty effect row, classified data, trap
// on overflow, no in-place mutation, no self-call and no stale read.  An
// axis whose grade is a fact, such as a source, a trust, a precision or a
// version, states no fact at its pole.  So the pack below is the whole
// claim of the binding.
//
// Read next: example_custom_optimizer.cpp adds Alloc, a linear cost and
// three buffers.  example_forge_phase.cpp binds an internal phase that
// does no in-place mutation.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Reductions.h>
#include <fixy/Tags.h>
#include <foundation/contracts/Pre.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace source = ::fixy::tags::source;
namespace trust_level = ::fixy::tags::trust;
using ::fixy::Axis;
using ::fixy::strict_pole_t;

// The signature of the kernel.  In production the user compiles it from
// a GEMM library of their own.  The scalar reference below is only a
// stand-in, because the grades describe the behavior of the function and
// not the quality of its code.
//
// Each matrix is a rank-2 strided view of the base: the output C of the
// sizes [M, N], the left factor A of the sizes [M, K] and the right factor
// B of the sizes [K, N].  The two factors take the roles LeftFactor and
// RightFactor, so a call cannot swap them, and the sizes M, N and K come
// from the views.
using GemmFp32Ptr = void (*)(::fixy::StridedView<float, 2> c, ::fixy::LeftFactorOf<float> const& a,
                             ::fixy::RightFactorOf<float> const& b) noexcept;

// C = A @ B.  Each output is the sum of its products in the order of k, from
// zero.  The precondition states that the sizes of the three views agree.
// Complexity: O(M N K).
void scalar_gemm_ref(::fixy::StridedView<float, 2> c, ::fixy::LeftFactorOf<float> const& a,
                     ::fixy::RightFactorOf<float> const& b) noexcept {
    ::fixy::StridedView<float const, 2> const left = a.value();
    ::fixy::StridedView<float const, 2> const right = b.value();
    std::size_t const m = left.size(::fixy::index<0>());
    std::size_t const k = left.size(::fixy::index<1>());
    std::size_t const n = right.size(::fixy::index<1>());
    CRUCIBLE_PRE(right.size(::fixy::index<0>()) == k && c.size(::fixy::index<0>()) == m
                 && c.size(::fixy::index<1>()) == n);
    static_cast<void>(::fixy::mint_strided_scope(c, [&](auto const out) noexcept {
        return ::fixy::mint_strided_scope(left, [&](auto const lhs) noexcept {
            return ::fixy::mint_strided_scope(right, [&](auto const rhs) noexcept {
                for (std::size_t row = 0; row < m; ++row) {
                    for (std::size_t col = 0; col < n; ++col) {
                        float acc = 0.f;
                        for (std::size_t p = 0; p < k; ++p) {
                            acc += lhs.at(lhs.index(row, p).expect("the precondition bounds the index of A"))
                                 * rhs.at(rhs.index(p, col).expect("the precondition bounds the index of B"));
                        }
                        out.at(out.index(row, col).expect("the precondition bounds the index of C")) = acc;
                    }
                }
                return ::fixy::Unit{};
            });
        });
    }));
}

// The rank-2 view of the M x N elements of run in row-major order.
template <class T, std::size_t Count>
[[nodiscard]] ::fixy::StridedView<T, 2> matrix_of(::fixy::View<T, Count> run,
                                                  ::fixy::FixedArray<std::size_t, 2> const& sizes) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const strides{sizes.at(::fixy::index<1>()), std::size_t{1}};
    return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("a dense matrix fits its run");
}

// The unique rank-2 view of the M x N elements of the run of a frame in
// row-major order.  The view takes the run, so it is the one writer of the run.
template <class T, class Brand, std::size_t Count>
[[nodiscard]] ::fixy::StridedViewMut<T, 2, Brand> matrix_of(::fixy::ViewMut<T, Brand, Count> run,
                                                            ::fixy::FixedArray<std::size_t, 2> const& sizes) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const strides{sizes.at(::fixy::index<1>()), std::size_t{1}};
    return ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
        .expect("a dense matrix fits its run");
}

// The pack of a user kernel on the background thread.  The alias names
// the pack one time, as a role of fixy/Role.h does, and
// fixy::mint_fn_for<UserBgKernel> is its door.
//
// The Security grade is as_public.  The value of the binding is a
// function pointer, which holds no secret.  The weights that the kernel
// reads are its arguments, and they carry their own grades.  The corpus
// of fixy/Corpus.h refuses a Bg row on an internal or a classified value,
// because the schedule of the background thread then depends on that
// value.
//
// The pack names no cost.  A GEMM is O(M N K), and the Complexity atoms
// state a constant, a linear or a quadratic cost only.  The pack names no
// lifetime either.  The one Lifetime atom is in_region, and no atom
// states the whole program, so a free function states no lifetime.
template <class Kernel>
using UserBgKernel = ::fixy::fn<Kernel,
                                at::copy,  // a function pointer is free to copy
                                at::with_bg,  // the kernel runs on the background thread only
                                at::as_public,  // the pointer holds no secret
                                at::from_source<source::FromUser>,  // the user supplied the kernel
                                at::trust<trust_level::Tested>,  // the user has tests, and no proof
                                at::precision_f32,  // the kernel accumulates in FP32
                                at::space_bounded<0>,  // the kernel writes C in place and allocates nothing
                                at::mut_mutable,  // the kernel writes the output buffer C
                                at::reentrant,  // two kernels can run in parallel
                                at::version<1>>;  // the first revision of this binding

using BoundGemm = UserBgKernel<GemmFp32Ptr>;

// The atoms are empty types, so the binding is the function pointer and
// nothing more.
static_assert(sizeof(BoundGemm) == sizeof(GemmFp32Ptr),
              "a binding must be byte-equivalent to its payload.  If this fires, an axis became a member "
              "instead of a type-level grade.");

// Each atom sets the grade on its own axis.
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Usage>, at::copy>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Effect>, at::with_bg>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Security>, at::as_public>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Provenance>, at::from_source<source::FromUser>>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Trust>, at::trust<trust_level::Tested>>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Precision>, at::precision_f32>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Mutation>, at::mut_mutable>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Reentrancy>, at::reentrant>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Version>, at::version<1>>);

// An axis that the pack does not name takes its strict pole.
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Overflow>, strict_pole_t<Axis::Overflow>>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Staleness>, strict_pole_t<Axis::Staleness>>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Refinement>, strict_pole_t<Axis::Refinement>>);
static_assert(!BoundGemm::mentions_axis<Axis::Complexity>);

// A caller needs a context whose row holds Bg.  The background drain
// context holds it, and the foreground context holds no effect.
static_assert(::fixy::CtxAdmitsBinding<::fixy::BgDrainCtx, BoundGemm>);
static_assert(!::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, BoundGemm>);

// Why the pack names as_public.  The same Bg row on the classified pole,
// or on as_internal, is refused.
static_assert(!::fixy::IsAccepted<GemmFp32Ptr, at::with_bg>);
static_assert(!::fixy::IsAccepted<GemmFp32Ptr, at::with_bg, at::as_internal>);

}  // namespace

int main() {
    // The mint is the only door to a binding.
    const BoundGemm bound = ::fixy::mint_fn_for<UserBgKernel>(&scalar_gemm_ref);

    constexpr std::size_t M = 2, N = 2, K = 2;
    const ::fixy::FixedArray<float, M * K> a{1.f, 2.f, 3.f, 4.f};
    const ::fixy::FixedArray<float, K * N> b{5.f, 6.f, 7.f, 8.f};
    ::fixy::FixedArray<float, M * N> c{};

    // The kernel writes C through the run that the frame door of c lends.  The
    // scope of the unique view of the run gives the kernel the plain view that
    // writes, for the call of the scope only.
    c.with_mut([&bound, &a, &b](auto run) noexcept {
        auto out = matrix_of(std::move(run), ::fixy::FixedArray<std::size_t, 2>{M, N});
        static_cast<void>(::fixy::mint_strided_scope(out, [&bound, &a, &b](auto const target) noexcept {
            bound.value()(target.view(),
                          ::fixy::side_as<::fixy::side::LeftFactor>(
                              matrix_of(a.view(), ::fixy::FixedArray<std::size_t, 2>{M, K})),
                          ::fixy::side_as<::fixy::side::RightFactor>(
                              matrix_of(b.view(), ::fixy::FixedArray<std::size_t, 2>{K, N})));
            return ::fixy::Unit{};
        }));
    });

    // C = [[1*5 + 2*7, 1*6 + 2*8], [3*5 + 4*7, 3*6 + 4*8]] = [[19, 22], [43, 50]].
    // Each product and each sum is a small integer, so the result is exact
    // and the check compares bits.
    const ::fixy::FixedArray<float, M * N> expected{19.f, 22.f, 43.f, 50.f};
    ::fixy::report(::fixy::Sink::Out, "custom_kernel result: [[{}, {}], [{}, {}]] (expected [[19, 22], [43, 50]])\n",
                   c.at(::fixy::index<0>()), c.at(::fixy::index<1>()), c.at(::fixy::index<2>()),
                   c.at(::fixy::index<3>()));
    for (::fixy::Index<M* N> const i : c.indices()) {
        if (::fixy::bit_cast<std::uint32_t>(c.at(i)) != ::fixy::bit_cast<std::uint32_t>(expected.at(i))) return 1;
    }

    ::fixy::report(::fixy::Sink::Out, "BoundGemm sizeof = {} (== sizeof(GemmFp32Ptr) {})\n", sizeof(BoundGemm),
                   sizeof(GemmFp32Ptr));
    return 0;
}
