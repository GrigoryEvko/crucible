#pragma once

// The loops of bench_fp_elementary and the doors of its reference units.
//
// Throughput form: each call takes one input of the view, and the loop folds
// the bits of each result into one word with an xor.  The calls do not
// depend on each other, and the xor of the integer pipes costs no FP pipe.
//
// Latency form: each call takes the input x + zero * y, where y is the result
// of the call before it and zero is 0 at run time.  The sum is exactly x for
// each finite y, so each call takes the input of the throughput form, and
// each call waits for the call before it.  The multiply and the add of the
// chain add their latency to each step, and the floor row measures it.
//
// Each unit instantiates the loops with its own kernel, so each unit has its
// own copy of the loop around its own call: the functions of fixy/fp inline
// into the loop, and a reference of a library is one call for each input.

#include <fixy/Core.h>

#include <cstdint>

namespace bench_fp_elementary {

// The function of a row.
enum class Function : std::uint8_t {
    Exp,
    Log,
    Erf,
};

// The xor of the bits of the result of each input.  O(count of inputs).
template <class Kernel>
[[nodiscard]] std::uint64_t xor_of_results(Kernel const& kernel, ::fixy::View<double const> inputs) noexcept {
    std::uint64_t folded = 0;
    for (double const input : inputs)
        folded ^= ::fixy::bit_cast<std::uint64_t>(kernel(input));
    return folded;
}

template <class Kernel>
[[nodiscard]] std::uint32_t xor_of_results(Kernel const& kernel, ::fixy::View<float const> inputs) noexcept {
    std::uint32_t folded = 0;
    for (float const input : inputs)
        folded ^= ::fixy::bit_cast<std::uint32_t>(kernel(input));
    return folded;
}

// The last result of the chain over the inputs.  O(count of inputs).
template <class Kernel, class Value>
[[nodiscard]] Value chain_of_results(Kernel const& kernel, ::fixy::View<Value const> inputs, Value zero) noexcept {
    Value result = 0;
    for (Value const input : inputs)
        result = kernel(input + zero * result);
    return result;
}

// The doors of the reference units: the two forms of each function of one
// reference, for double and for float.  A unit that the build omits gives
// no door, and the bench omits its rows (bench/CMakeLists.txt).
namespace glibc {
[[nodiscard]] std::uint64_t xor_of(Function function, ::fixy::View<double const> inputs) noexcept;
[[nodiscard]] std::uint32_t xor_of(Function function, ::fixy::View<float const> inputs) noexcept;
[[nodiscard]] double chain_of(Function function, ::fixy::View<double const> inputs, double zero) noexcept;
[[nodiscard]] float chain_of(Function function, ::fixy::View<float const> inputs, float zero) noexcept;
}  // namespace glibc

namespace core_math {
[[nodiscard]] std::uint64_t xor_of(Function function, ::fixy::View<double const> inputs) noexcept;
[[nodiscard]] std::uint32_t xor_of(Function function, ::fixy::View<float const> inputs) noexcept;
[[nodiscard]] double chain_of(Function function, ::fixy::View<double const> inputs, double zero) noexcept;
[[nodiscard]] float chain_of(Function function, ::fixy::View<float const> inputs, float zero) noexcept;
}  // namespace core_math

namespace sleef {
[[nodiscard]] std::uint64_t xor_of(Function function, ::fixy::View<double const> inputs) noexcept;
[[nodiscard]] std::uint32_t xor_of(Function function, ::fixy::View<float const> inputs) noexcept;
[[nodiscard]] double chain_of(Function function, ::fixy::View<double const> inputs, double zero) noexcept;
[[nodiscard]] float chain_of(Function function, ::fixy::View<float const> inputs, float zero) noexcept;
}  // namespace sleef

}  // namespace bench_fp_elementary
