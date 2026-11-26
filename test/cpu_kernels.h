#pragma once

// Reference kernels for tests that need a dispatch pipeline to carry
// real numbers.  They are written for obvious correctness, not for
// speed, and none of them belongs in production.
//
// Each kernel reads its inputs as View<float const> and writes its output
// through the ViewMut of a frame, and every array is row-major.  Two inputs
// of one type come in their roles (Side), and each dimension is a count of
// elements in a role of its own (Dim), so no call can swap two of them.  A
// kernel first checks that each run holds the elements that its dimensions
// name, and each read and write then checks its position, so no kernel
// reaches past a run.  Each kernel keeps the order of its float operations,
// so its result has the same bits on each run.

#include <crucible/CrucibleContext.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Product.h>
#include <foundation/effects/Effect.h>
#include <cmath>
#include <cstddef>

namespace crucible::cpu {

// The roles of the inputs and of the dimensions of the kernels.
namespace side {

// The two operands of an elementwise add, and of a difference.
struct Augend final {};
struct Addend final {};
struct Minuend final {};
struct Subtrahend final {};
// The gradient that the backward pass of a rectifier gates, and the input
// of the forward pass that gates it.
struct Gradient final {};
struct Activation final {};
// The scale and the shift of a normalization, for each column or channel.
struct Scale final {};
struct Shift final {};
// The weights of a convolution.
struct Filter final {};
// The keys and the values of an attention.
struct Keys final {};
struct Values final {};
// The class of each sample of a loss.
struct Targets final {};
// The value that the larger of two floats compares with the running value.
struct Candidate final {};

// The dimensions.
struct Count final {};  // the elements of an elementwise kernel
struct Period final {};  // the elements of a second operand that tiles over the first
struct Rows final {};  // the rows of a matrix, or of a run of rows
struct Columns final {};  // the columns of the output of a matrix product
struct Inner final {};  // the shared dimension of a matrix product
struct Width final {};  // the elements of one row of a row-wise kernel
struct Batch final {};  // the samples of a batch
struct Channels final {};  // the channels of each sample
struct OutChannels final {};  // the channels of the output of a convolution
struct Positions final {};  // the positions of one channel of a sample
struct Height final {};  // the rows of one channel of a sample
struct WidthOfImage final {};  // the columns of one channel of a sample
struct KernelHeight final {};  // the rows of the window of a convolution
struct KernelWidth final {};  // the columns of the window of a convolution
struct Window final {};  // the side of the square window of a pool
struct Sequence final {};  // the positions of one sequence
struct Selected final {};  // the position that a gather takes from each sequence

}  // namespace side

// A dimension of a kernel: a count of elements in its role.
template <class Role>
using Dim = ::fixy::Side<Role, ::fixy::ElementCount>;

// The dimension of count elements in the role Role.
template <class Role>
[[nodiscard]] constexpr Dim<Role> make_dim(std::size_t count) noexcept {
    return ::fixy::side_as<Role>(::fixy::ElementCount{count});
}

// The run of an input of a kernel in the role Role.  A View of floats that
// the caller can write converts to the read-only View of the role.
template <class Role>
[[nodiscard]] constexpr ::fixy::Side<Role, ::fixy::View<float const>>
make_input(::fixy::View<float const> run) noexcept {
    return ::fixy::side_as<Role>(run);
}

// The position of an element in a run.
using Offset = ::fixy::Id<struct OffsetTag, std::size_t>;

namespace detail {

// The count of a dimension.
template <class Role>
[[nodiscard]] constexpr std::size_t count_of(Dim<Role> const& dim) noexcept {
    return dim.value().raw();
}

// The element at a position of a run.  A position at or past the count
// ends the process, so a kernel never reads or writes outside its run.
template <class Element>
[[nodiscard]] Element& element_at(::fixy::View<Element> run, Offset position) noexcept {
    return *run.get(position.raw()).expect("a reference kernel reads and writes inside its runs");
}

// The writer of the element at a position of the output of a kernel.  A
// position at or past the count ends the process.
template <class Brand, std::size_t Extent>
[[nodiscard]] auto writer_at(::fixy::ViewMut<float, Brand, Extent>& run, Offset position) noexcept {
    return run.get_mut(position.raw()).expect("a reference kernel reads and writes inside its runs");
}

// Writes value into the element at a position of the output of a kernel.
template <class Brand, std::size_t Extent>
void write_at(::fixy::ViewMut<float, Brand, Extent>& run, Offset position, float value) noexcept {
    auto element = writer_at(run, position);
    *element = value;
}

// The value of the element at a position of the output of a kernel.  The
// const reference reads the run, and it does not write it.
template <class Brand, std::size_t Extent>
[[nodiscard]] float read_at(::fixy::ViewMut<float, Brand, Extent> const& run, Offset position) noexcept {
    return *run.get(position.raw()).expect("a reference kernel reads and writes inside its runs");
}

// The larger of two floats with the rule of std::max: the second value only
// when the first is less, so a NaN first value stays.
[[nodiscard]] constexpr float max_keep_first(float first, ::fixy::Side<side::Candidate, float> second) noexcept {
    return first < second.value() ? second.value() : first;
}

}  // namespace detail

// C[M,N] = A[M,K] times B[K,N].
template <class Brand, std::size_t Extent>
void mm(::fixy::TestRunnerCtx const& test,
        ::fixy::Side<::foundation::core::side::LeftFactor, ::fixy::View<float const>> A,
        ::fixy::Side<::foundation::core::side::RightFactor, ::fixy::View<float const>> B,
        ::fixy::ViewMut<float, Brand, Extent> C, Dim<side::Rows> rows, Dim<side::Columns> columns,
        Dim<side::Inner> inner) {
    std::size_t const M = detail::count_of(rows);
    std::size_t const N = detail::count_of(columns);
    std::size_t const K = detail::count_of(inner);
    CRUCIBLE_TEST_ASSERT(test, A.value().size() >= M * K && B.value().size() >= K * N && C.size() >= M * N);
    for (std::size_t i = 0; i < M; i++) {
        for (std::size_t j = 0; j < N; j++) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < K; k++)
                sum +=
                    detail::element_at(A.value(), Offset{i * K + k}) * detail::element_at(B.value(), Offset{k * N + j});
            detail::write_at(C, Offset{i * N + j}, sum);
        }
    }
}

// C[n] = A[n] + B[n].  A shorter second operand is tiled over the
// first, so a period equal to n is a plain elementwise add and a period
// equal to the last dimension is a bias add.  A period of zero is n.
template <class Brand, std::size_t Extent>
void add(::fixy::TestRunnerCtx const& test, ::fixy::Side<side::Augend, ::fixy::View<float const>> A,
         ::fixy::Side<side::Addend, ::fixy::View<float const>> B, ::fixy::ViewMut<float, Brand, Extent> C,
         Dim<side::Count> count, Dim<side::Period> period = make_dim<side::Period>(0)) {
    std::size_t const n = detail::count_of(count);
    std::size_t const bias_n = detail::count_of(period) == 0 ? n : detail::count_of(period);
    CRUCIBLE_TEST_ASSERT(test, A.value().size() >= n && B.value().size() >= bias_n && C.size() >= n);
    for (std::size_t i = 0; i < n; i++)
        detail::write_at(C, Offset{i},
                         detail::element_at(A.value(), Offset{i}) + detail::element_at(B.value(), Offset{i % bias_n}));
}

// C[n] = A[n] - B[n].
template <class Brand, std::size_t Extent>
void sub(::fixy::TestRunnerCtx const& test, ::fixy::Side<side::Minuend, ::fixy::View<float const>> A,
         ::fixy::Side<side::Subtrahend, ::fixy::View<float const>> B, ::fixy::ViewMut<float, Brand, Extent> C,
         Dim<side::Count> count) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, A.value().size() >= n && B.value().size() >= n && C.size() >= n);
    for (std::size_t i = 0; i < n; i++)
        detail::write_at(C, Offset{i},
                         detail::element_at(A.value(), Offset{i}) - detail::element_at(B.value(), Offset{i}));
}

// C[n] = A[n] times B[n].
template <class Brand, std::size_t Extent>
void mul(::fixy::TestRunnerCtx const& test,
         ::fixy::Side<::foundation::core::side::LeftFactor, ::fixy::View<float const>> A,
         ::fixy::Side<::foundation::core::side::RightFactor, ::fixy::View<float const>> B,
         ::fixy::ViewMut<float, Brand, Extent> C, Dim<side::Count> count) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, A.value().size() >= n && B.value().size() >= n && C.size() >= n);
    for (std::size_t i = 0; i < n; i++)
        detail::write_at(C, Offset{i},
                         detail::element_at(A.value(), Offset{i}) * detail::element_at(B.value(), Offset{i}));
}

// C[n] = max(A[n], 0).
template <class Brand, std::size_t Extent>
void relu(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> A, ::fixy::ViewMut<float, Brand, Extent> C,
          Dim<side::Count> count) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, A.size() >= n && C.size() >= n);
    for (std::size_t i = 0; i < n; i++) {
        float const value = detail::element_at(A, Offset{i});
        detail::write_at(C, Offset{i}, value > 0.0f ? value : 0.0f);
    }
}

// C[n] = grad[n] where the input was positive, and zero elsewhere.
template <class Brand, std::size_t Extent>
void relu_bwd(::fixy::TestRunnerCtx const& test, ::fixy::Side<side::Gradient, ::fixy::View<float const>> grad,
              ::fixy::Side<side::Activation, ::fixy::View<float const>> input, ::fixy::ViewMut<float, Brand, Extent> C,
              Dim<side::Count> count) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, grad.value().size() >= n && input.value().size() >= n && C.size() >= n);
    for (std::size_t i = 0; i < n; i++)
        detail::write_at(
            C, Offset{i},
            detail::element_at(input.value(), Offset{i}) > 0.0f ? detail::element_at(grad.value(), Offset{i}) : 0.0f);
}

// C[n] = A[n] * 0.5 * (1 + erf(A[n] / sqrt(2))).
template <class Brand, std::size_t Extent>
void gelu(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> A, ::fixy::ViewMut<float, Brand, Extent> C,
          Dim<side::Count> count) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, A.size() >= n && C.size() >= n);
    constexpr float k = 0.7071067811865476f;  // 1/sqrt(2)
    for (std::size_t i = 0; i < n; i++) {
        float const value = detail::element_at(A, Offset{i});
        detail::write_at(C, Offset{i}, value * 0.5f * (1.0f + std::erf(value * k)));
    }
}

// C[n] = 1 / (1 + exp(-A[n])).
template <class Brand, std::size_t Extent>
void sigmoid(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> A, ::fixy::ViewMut<float, Brand, Extent> C,
             Dim<side::Count> count) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, A.size() >= n && C.size() >= n);
    for (std::size_t i = 0; i < n; i++)
        detail::write_at(C, Offset{i}, 1.0f / (1.0f + std::exp(-detail::element_at(A, Offset{i}))));
}

// C[n] = A[n] * sigmoid(A[n]).
template <class Brand, std::size_t Extent>
void silu(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> A, ::fixy::ViewMut<float, Brand, Extent> C,
          Dim<side::Count> count) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, A.size() >= n && C.size() >= n);
    for (std::size_t i = 0; i < n; i++) {
        float const value = detail::element_at(A, Offset{i});
        float s = 1.0f / (1.0f + std::exp(-value));
        detail::write_at(C, Offset{i}, value * s);
    }
}

// C[n] = value.
template <class Brand, std::size_t Extent>
void fill(::fixy::TestRunnerCtx const& test, ::fixy::ViewMut<float, Brand, Extent> C, Dim<side::Count> count,
          float value) {
    std::size_t const n = detail::count_of(count);
    CRUCIBLE_TEST_ASSERT(test, C.size() >= n);
    for (std::size_t i = 0; i < n; i++)
        detail::write_at(C, Offset{i}, value);
}

// Y[B,D] = softmax of X[B,D] over the last dimension.  The row
// maximum is subtracted before the exponential, so the largest
// exponent is zero and no term can overflow.
template <class Brand, std::size_t Extent>
void softmax(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X, ::fixy::ViewMut<float, Brand, Extent> Y,
             Dim<side::Rows> rows, Dim<side::Width> width) {
    std::size_t const B = detail::count_of(rows);
    std::size_t const D = detail::count_of(width);
    CRUCIBLE_TEST_ASSERT(test, X.size() >= B * D && Y.size() >= B * D && D > 0);
    for (std::size_t b = 0; b < B; b++) {
        std::size_t const row = b * D;
        float mx = detail::element_at(X, Offset{row});
        for (std::size_t d = 1; d < D; d++)
            mx = detail::max_keep_first(mx, ::fixy::side_as<side::Candidate>(detail::element_at(X, Offset{row + d})));
        float sum = 0.0f;
        for (std::size_t d = 0; d < D; d++) {
            auto element = detail::writer_at(Y, Offset{row + d});
            *element = std::exp(detail::element_at(X, Offset{row + d}) - mx);
            sum += *element;
        }
        float inv = 1.0f / sum;
        for (std::size_t d = 0; d < D; d++) {
            auto element = detail::writer_at(Y, Offset{row + d});
            *element *= inv;
        }
    }
}

// Y[B,D] = (X - mean) / sqrt(var + eps), scaled by gamma[D] and
// shifted by beta[D], with both statistics taken over each row.
template <class Brand, std::size_t Extent>
void layer_norm(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X,
                ::fixy::Side<side::Scale, ::fixy::View<float const>> gamma,
                ::fixy::Side<side::Shift, ::fixy::View<float const>> beta, ::fixy::ViewMut<float, Brand, Extent> Y,
                Dim<side::Rows> rows, Dim<side::Width> width, float eps = 1e-5f) {
    std::size_t const B = detail::count_of(rows);
    std::size_t const D = detail::count_of(width);
    CRUCIBLE_TEST_ASSERT(test, X.size() >= B * D && gamma.value().size() >= D && beta.value().size() >= D
                                   && Y.size() >= B * D);
    for (std::size_t b = 0; b < B; b++) {
        std::size_t const row = b * D;
        float mean = 0.0f;
        for (std::size_t d = 0; d < D; d++)
            mean += detail::element_at(X, Offset{row + d});
        mean /= static_cast<float>(D);
        float var = 0.0f;
        for (std::size_t d = 0; d < D; d++) {
            float diff = detail::element_at(X, Offset{row + d}) - mean;
            var += diff * diff;
        }
        var /= static_cast<float>(D);
        float inv = 1.0f / std::sqrt(var + eps);
        for (std::size_t d = 0; d < D; d++)
            detail::write_at(Y, Offset{row + d},
                             (detail::element_at(X, Offset{row + d}) - mean) * inv
                                     * detail::element_at(gamma.value(), Offset{d})
                                 + detail::element_at(beta.value(), Offset{d}));
    }
}

// Y[N,C,HW], normalized per channel.  The mean and variance are taken
// over all positions of all samples in a channel, and gamma[C] and
// beta[C] scale and shift it.
template <class Brand, std::size_t Extent>
void batch_norm(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X,
                ::fixy::Side<side::Scale, ::fixy::View<float const>> gamma,
                ::fixy::Side<side::Shift, ::fixy::View<float const>> beta, ::fixy::ViewMut<float, Brand, Extent> Y,
                Dim<side::Batch> batch, Dim<side::Channels> channels, Dim<side::Positions> positions,
                float eps = 1e-5f) {
    std::size_t const N = detail::count_of(batch);
    std::size_t const C = detail::count_of(channels);
    std::size_t const HW = detail::count_of(positions);
    CRUCIBLE_TEST_ASSERT(test, X.size() >= N * C * HW && gamma.value().size() >= C && beta.value().size() >= C
                                   && Y.size() >= N * C * HW);
    for (std::size_t c = 0; c < C; c++) {
        float mean = 0.0f;
        std::size_t count = N * HW;
        for (std::size_t n = 0; n < N; n++)
            for (std::size_t hw = 0; hw < HW; hw++)
                mean += detail::element_at(X, Offset{(n * C + c) * HW + hw});
        mean /= static_cast<float>(count);
        float var = 0.0f;
        for (std::size_t n = 0; n < N; n++)
            for (std::size_t hw = 0; hw < HW; hw++) {
                float d = detail::element_at(X, Offset{(n * C + c) * HW + hw}) - mean;
                var += d * d;
            }
        var /= static_cast<float>(count);
        float inv = 1.0f / std::sqrt(var + eps);
        for (std::size_t n = 0; n < N; n++)
            for (std::size_t hw = 0; hw < HW; hw++) {
                Offset const idx{(n * C + c) * HW + hw};
                detail::write_at(Y, idx,
                                 (detail::element_at(X, idx) - mean) * inv
                                         * detail::element_at(gamma.value(), Offset{c})
                                     + detail::element_at(beta.value(), Offset{c}));
            }
    }
}

// Y[N,Co,Ho,Wo] from X[N,Ci,Hi,Wi] and W[Co,Ci,Kh,Kw].  There is no
// padding and the stride is one, so Ho is Hi - Kh + 1 and Wo is
// Wi - Kw + 1.
template <class Brand, std::size_t Extent>
void conv2d(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X,
            ::fixy::Side<side::Filter, ::fixy::View<float const>> W, ::fixy::ViewMut<float, Brand, Extent> Y,
            Dim<side::Batch> batch, Dim<side::Channels> channels, Dim<side::Height> height,
            Dim<side::WidthOfImage> image_width, Dim<side::OutChannels> out_channels,
            Dim<side::KernelHeight> kernel_height, Dim<side::KernelWidth> kernel_width) {
    std::size_t const N = detail::count_of(batch);
    std::size_t const Ci = detail::count_of(channels);
    std::size_t const Hi = detail::count_of(height);
    std::size_t const Wi = detail::count_of(image_width);
    std::size_t const Co = detail::count_of(out_channels);
    std::size_t const Kh = detail::count_of(kernel_height);
    std::size_t const Kw = detail::count_of(kernel_width);
    CRUCIBLE_TEST_ASSERT(test, Kh >= 1 && Kh <= Hi && Kw >= 1 && Kw <= Wi);
    std::size_t Ho = Hi - Kh + 1;
    std::size_t Wo = Wi - Kw + 1;
    CRUCIBLE_TEST_ASSERT(test, X.size() >= N * Ci * Hi * Wi && W.value().size() >= Co * Ci * Kh * Kw
                                   && Y.size() >= N * Co * Ho * Wo);
    Y = ::fixy::reborrow(std::move(Y), [count = N * Co * Ho * Wo](auto output) noexcept {
        ::fixy::fill(::fixy::take_window(std::move(output), 0, count).expect("the check above bounds the output"),
                     0.0f);
    });
    for (std::size_t n = 0; n < N; n++)
        for (std::size_t co = 0; co < Co; co++)
            for (std::size_t ci = 0; ci < Ci; ci++)
                for (std::size_t ho = 0; ho < Ho; ho++)
                    for (std::size_t wo = 0; wo < Wo; wo++)
                        for (std::size_t kh = 0; kh < Kh; kh++)
                            for (std::size_t kw = 0; kw < Kw; kw++) {
                                auto element = detail::writer_at(Y, Offset{((n * Co + co) * Ho + ho) * Wo + wo});
                                *element +=
                                    detail::element_at(X, Offset{((n * Ci + ci) * Hi + (ho + kh)) * Wi + (wo + kw)})
                                    * detail::element_at(W.value(), Offset{((co * Ci + ci) * Kh + kh) * Kw + kw});
                            }
}

// Y[N,C,H/K,W/K], the maximum over each K by K window.
template <class Brand, std::size_t Extent>
void maxpool2d(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X, ::fixy::ViewMut<float, Brand, Extent> Y,
               Dim<side::Batch> batch, Dim<side::Channels> channels, Dim<side::Height> height,
               Dim<side::WidthOfImage> image_width, Dim<side::Window> window = make_dim<side::Window>(2)) {
    std::size_t const N = detail::count_of(batch);
    std::size_t const C = detail::count_of(channels);
    std::size_t const H = detail::count_of(height);
    std::size_t const W = detail::count_of(image_width);
    std::size_t const K = detail::count_of(window);
    CRUCIBLE_TEST_ASSERT(test, K > 0);
    std::size_t Ho = H / K, Wo = W / K;
    CRUCIBLE_TEST_ASSERT(test, X.size() >= N * C * H * W && Y.size() >= N * C * Ho * Wo);
    for (std::size_t n = 0; n < N; n++)
        for (std::size_t c = 0; c < C; c++)
            for (std::size_t ho = 0; ho < Ho; ho++)
                for (std::size_t wo = 0; wo < Wo; wo++) {
                    float mx = -::fixy::infinity_of<float>;
                    for (std::size_t kh = 0; kh < K; kh++)
                        for (std::size_t kw = 0; kw < K; kw++)
                            mx = detail::max_keep_first(
                                mx, ::fixy::side_as<side::Candidate>(detail::element_at(
                                        X, Offset{((n * C + c) * H + ho * K + kh) * W + wo * K + kw})));
                    detail::write_at(Y, Offset{((n * C + c) * Ho + ho) * Wo + wo}, mx);
                }
}

// Y[N,C] = the mean of X[N,C,HW] over all positions.
template <class Brand, std::size_t Extent>
void avgpool_global(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X,
                    ::fixy::ViewMut<float, Brand, Extent> Y, Dim<side::Batch> batch, Dim<side::Channels> channels,
                    Dim<side::Positions> positions) {
    std::size_t const N = detail::count_of(batch);
    std::size_t const C = detail::count_of(channels);
    std::size_t const HW = detail::count_of(positions);
    CRUCIBLE_TEST_ASSERT(test, X.size() >= N * C * HW && Y.size() >= N * C);
    float inv = 1.0f / static_cast<float>(HW);
    for (std::size_t n = 0; n < N; n++)
        for (std::size_t c = 0; c < C; c++) {
            float sum = 0.0f;
            for (std::size_t hw = 0; hw < HW; hw++)
                sum += detail::element_at(X, Offset{(n * C + c) * HW + hw});
            detail::write_at(Y, Offset{n * C + c}, sum * inv);
        }
}

// Y[B,S,D] from Q, K and V, each [B,S,D].  Y is the softmax of Q
// times the transpose of K, divided by the square root of D, times V.
//
// The attention weights of one sequence form an S by S matrix in a block
// of the heap of the test, which this never runs anywhere but a test.
template <class Brand, std::size_t Extent>
void sdpa(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> Q,
          ::fixy::Side<side::Keys, ::fixy::View<float const>> K,
          ::fixy::Side<side::Values, ::fixy::View<float const>> V, ::fixy::ViewMut<float, Brand, Extent> Y,
          Dim<side::Batch> batch, Dim<side::Sequence> sequence, Dim<side::Width> width) {
    std::size_t const B = detail::count_of(batch);
    std::size_t const S = detail::count_of(sequence);
    std::size_t const D = detail::count_of(width);
    CRUCIBLE_TEST_ASSERT(test, Q.size() >= B * S * D && K.value().size() >= B * S * D && V.value().size() >= B * S * D
                                   && Y.size() >= B * S * D && S > 0);
    float scale = 1.0f / std::sqrt(static_cast<float>(D));

    ::fixy::HeapArray<float> weights =
        ::fixy::mint_heap_array_zeroed<float>(::foundation::effects::testing::test().alloc, ::fixy::strict_mul(S, S));

    // Each sequence writes the weights again, so one frame of the weights
    // holds the loop over the batch.
    weights = ::fixy::with_mut(std::move(weights), [B, S, D, Q, K, V, &Y, scale](auto attn) noexcept {
        for (std::size_t b = 0; b < B; b++) {
            std::size_t const base = b * S * D;

            for (std::size_t i = 0; i < S; i++)
                for (std::size_t j = 0; j < S; j++) {
                    float dot = 0.0f;
                    for (std::size_t d = 0; d < D; d++)
                        dot += detail::element_at(Q, Offset{base + i * D + d})
                             * detail::element_at(K.value(), Offset{base + j * D + d});
                    detail::write_at(attn, Offset{i * S + j}, dot * scale);
                }

            for (std::size_t i = 0; i < S; i++) {
                std::size_t const row = i * S;
                float mx = detail::read_at(attn, Offset{row});
                for (std::size_t j = 1; j < S; j++)
                    mx = detail::max_keep_first(
                        mx, ::fixy::side_as<side::Candidate>(detail::read_at(attn, Offset{row + j})));
                float sum = 0.0f;
                for (std::size_t j = 0; j < S; j++) {
                    auto element = detail::writer_at(attn, Offset{row + j});
                    *element = std::exp(*element - mx);
                    sum += *element;
                }
                float inv = 1.0f / sum;
                for (std::size_t j = 0; j < S; j++) {
                    auto element = detail::writer_at(attn, Offset{row + j});
                    *element *= inv;
                }
            }

            for (std::size_t i = 0; i < S; i++)
                for (std::size_t d = 0; d < D; d++) {
                    float sum = 0.0f;
                    for (std::size_t j = 0; j < S; j++)
                        sum += detail::read_at(attn, Offset{i * S + j})
                             * detail::element_at(V.value(), Offset{base + j * D + d});
                    detail::write_at(Y, Offset{base + i * D + d}, sum);
                }
        }
    });
}

// The mean over the batch of the negative log of the softmax
// probability at the target class.  logits is [B,C], targets is [B],
// and probs is a [B,C] scratch buffer the caller supplies, which is
// left holding the softmax on return.
template <class Brand, std::size_t Extent>
float cross_entropy(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> logits,
                    ::fixy::Side<side::Targets, ::fixy::View<int const>> targets,
                    ::fixy::ViewMut<float, Brand, Extent> probs, Dim<side::Rows> rows, Dim<side::Width> classes) {
    std::size_t const B = detail::count_of(rows);
    std::size_t const C = detail::count_of(classes);
    CRUCIBLE_TEST_ASSERT(test, targets.value().size() >= B);
    probs = ::fixy::reborrow(std::move(probs), [&test, logits, rows, classes](auto output) noexcept {
        softmax(test, logits, std::move(output), rows, classes);
    });
    float loss = 0.0f;
    for (std::size_t b = 0; b < B; b++) {
        int const target = detail::element_at(targets.value(), Offset{b});
        CRUCIBLE_TEST_ASSERT(test, target >= 0 && static_cast<std::size_t>(target) < C);
        loss -=
            std::log(detail::max_keep_first(detail::read_at(probs, Offset{b * C + static_cast<std::size_t>(target)}),
                                            ::fixy::side_as<side::Candidate>(1e-7f)));
    }
    return loss / static_cast<float>(B);
}

// Y[B,D] = X[B,idx,D], one position taken from every sequence.
template <class Brand, std::size_t Extent>
void index_select(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X,
                  ::fixy::ViewMut<float, Brand, Extent> Y, Dim<side::Batch> batch, Dim<side::Sequence> sequence,
                  Dim<side::Width> width, Dim<side::Selected> selected) {
    std::size_t const B = detail::count_of(batch);
    std::size_t const S = detail::count_of(sequence);
    std::size_t const D = detail::count_of(width);
    std::size_t const idx = detail::count_of(selected);
    CRUCIBLE_TEST_ASSERT(test, idx < S && X.size() >= B * S * D && Y.size() >= B * D);
    for (std::size_t b = 0; b < B; b++)
        Y = ::fixy::reborrow(std::move(Y), [X, b, S, D, idx](auto output) noexcept {
            ::fixy::copy(::fixy::take_window(std::move(output), b * D, D).expect("the check above bounds the output"),
                         X.window((b * S + idx) * D, D).expect("the check above bounds the input"))
                .expect("the two windows hold one count of elements");
        });
}

// Y[batch,N,M] = X[batch,M,N], transposing the last two dimensions.
template <class Brand, std::size_t Extent>
void transpose_2d(::fixy::TestRunnerCtx const& test, ::fixy::View<float const> X,
                  ::fixy::ViewMut<float, Brand, Extent> Y, Dim<side::Batch> batch, Dim<side::Rows> rows,
                  Dim<side::Columns> columns) {
    std::size_t const batches = detail::count_of(batch);
    std::size_t const M = detail::count_of(rows);
    std::size_t const N = detail::count_of(columns);
    CRUCIBLE_TEST_ASSERT(test, X.size() >= batches * M * N && Y.size() >= batches * N * M);
    for (std::size_t b = 0; b < batches; b++) {
        std::size_t const base = b * M * N;
        for (std::size_t i = 0; i < M; i++)
            for (std::size_t j = 0; j < N; j++)
                detail::write_at(Y, Offset{base + j * M + i}, detail::element_at(X, Offset{base + i * N + j}));
    }
}

// ── The ops of a replay ────────────────────────────────────────────────
//
// A test that replays a region through a compiled scope reads each input of
// the matched op through input_view, and it writes the one output of the op
// through with_output_views.  The kernel takes the Views of the inputs by
// value, read before the frame of the output, and it gets the ViewMut of the
// output for the time of the call.

// The input at a position of the matched op, as floats.  An input that the
// scope gives no View of floats ends the process.
[[nodiscard]] inline ::fixy::View<float const> read_input(CompiledScope const& scope, InputPosition position) {
    return scope.input_view<float>(position).expect("each input of a reference op is a slot of floats");
}

// Runs kernel on the ViewMut of the one output of the matched op.  A refusal
// of the outputs, or an op with a count of outputs other than one, fails the
// test.
template <class Kernel>
void write_output(::fixy::TestRunnerCtx const& test, CompiledScope& scope, Kernel const& kernel) {
    std::size_t outputs = 0;
    const auto written = scope.with_output_views<float>([&kernel, &outputs](auto& views) noexcept {
        std::move(views).drain([&kernel, &outputs](auto view) noexcept {
            kernel(std::move(view));
            ++outputs;
        });
    });
    CRUCIBLE_TEST_ASSERT(test, written.is_ok() && outputs == 1);
}

}  // namespace crucible::cpu
