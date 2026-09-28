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
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Tags.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace source = ::fixy::tags::source;
using ::fixy::Axis;
using ::fixy::axis_traits;

// The signature of the kernel.  In production the user compiles it from
// a GEMM library of their own.  The scalar reference below is only a
// stand-in, because the grades describe the behavior of the function and
// not the quality of its code.
using GemmFp32Ptr = void (*)(const float* a,  // M x K, row-major
                             const float* b,  // K x N, row-major
                             float* c,  // M x N, row-major, the output
                             int m, int n, int k) noexcept;

void scalar_gemm_ref(const float* a, const float* b, float* c, int m, int n, int k) noexcept {
    for (int row = 0; row < m; ++row) {
        for (int col = 0; col < n; ++col) {
            float acc = 0.f;
            for (int p = 0; p < k; ++p) {
                acc += a[row * k + p] * b[p * n + col];
            }
            c[row * n + col] = acc;
        }
    }
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
                                at::trust_tested,  // the user has tests, and no proof
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
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Trust>, at::trust_tested>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Precision>, at::precision_f32>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Mutation>, at::mut_mutable>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Reentrancy>, at::reentrant>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Version>, at::version<1>>);

// An axis that the pack does not name takes its strict pole.
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Overflow>, axis_traits<Axis::Overflow>::strict>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Staleness>, axis_traits<Axis::Staleness>::strict>);
static_assert(std::is_same_v<BoundGemm::grade_on<Axis::Refinement>, axis_traits<Axis::Refinement>::strict>);
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

    constexpr int M = 2, N = 2, K = 2;
    const float a[M * K] = {1.f, 2.f, 3.f, 4.f};
    const float b[K * N] = {5.f, 6.f, 7.f, 8.f};
    float c[M * N] = {};

    bound.value()(a, b, c, M, N, K);

    // C = [[1*5 + 2*7, 1*6 + 2*8], [3*5 + 4*7, 3*6 + 4*8]] = [[19, 22], [43, 50]].
    // Each product and each sum is a small integer, so the result is exact
    // and the check compares bits.
    const float expected[M * N] = {19.f, 22.f, 43.f, 50.f};
    std::printf("custom_kernel result: [[%g, %g], [%g, %g]] (expected [[19, 22], [43, 50]])\n",
                static_cast<double>(c[0]), static_cast<double>(c[1]), static_cast<double>(c[2]),
                static_cast<double>(c[3]));
    for (int i = 0; i < M * N; ++i) {
        if (std::bit_cast<std::uint32_t>(c[i]) != std::bit_cast<std::uint32_t>(expected[i])) return 1;
    }

    std::printf("BoundGemm sizeof = %zu (== sizeof(GemmFp32Ptr) %zu)\n", sizeof(BoundGemm), sizeof(GemmFp32Ptr));
    return 0;
}
