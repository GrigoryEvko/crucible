#pragma once

// The eager word of PyTorch: the state of the front end that changes the
// numerics of its eager ops, in one EagerNumerics word (crucible/
// NumericalRecipe.h).  The Vigil folds the word into the hash of each region,
// so a region records under the numerics that its ops ran with.
//
// The word holds four fields:
//
//   - The precision of a float32 matmul, from the context of PyTorch
//   - The TF32 bit of a cuBLAS matmul, from the context of PyTorch
//   - The TF32 bit of a cuDNN convolution, from the context of PyTorch
//   - The flush of denormal floats, from the FP control register of the
//     thread (foundation/os/FpControl.h).  PyTorch has no getter for it:
//     torch.set_flush_denormal writes the register and keeps no state.
//
// The vessel reads the word at the attach and at each region entry
// (DispatchResult::enters_region), never for each op.  The reads ask the
// context of PyTorch and the register, so they are in an ABI region.

#include <crucible/NumericalRecipe.h>
#include <crucible/Vigil.h>
#include <foundation/Quarantine.h>
#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

#include <ATen/Context.h>

#include <cstdint>

namespace crucible::vessel {

// The word of the calling thread.  The FP control register belongs to the
// thread, and the recording thread is the producer thread of the Vigil, so
// the word is the word that its ops run with.  A flush of the results or a
// flush of the inputs each sets FLUSH_DENORMAL.  A precision that the vessel
// does not name gives none, because the precision comes from PyTorch, which is
// outside the program.  The getters of PyTorch raise c10::Error for a mix of
// the legacy and the new API of the TF32 bits, so the read can raise.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the reader asks the context of PyTorch for the precision and the TF32 bits")
[[nodiscard]] inline ::fixy::Option<crucible::EagerNumerics> read_eager_numerics() {
    // The ordinal of a precision of PyTorch is the value of the field
    // MATMUL_PRECISION_MASK.
    static_assert(static_cast<uint32_t>(at::Float32MatmulPrecision::HIGHEST) == 0);
    static_assert(static_cast<uint32_t>(at::Float32MatmulPrecision::HIGH) == 1);
    static_assert(static_cast<uint32_t>(at::Float32MatmulPrecision::MEDIUM) == 2);
    const at::Context& context = at::globalContext();
    const auto precision = static_cast<uint32_t>(context.float32MatmulPrecision());
    const bool is_matmul_tf32 = context.allowTF32CuBLAS();
    const bool is_cudnn_tf32 = context.allowTF32CuDNN();
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    if (precision >= crucible::EagerNumerics::MATMUL_PRECISION_MASK) [[unlikely]]
        return ::fixy::none;
    uint32_t bits = precision;
    if (is_matmul_tf32) bits |= crucible::EagerNumerics::MATMUL_TF32;
    if (is_cudnn_tf32) bits |= crucible::EagerNumerics::CUDNN_TF32;

    const ::foundation::core::FpMode mode = ::foundation::os::FpControlWord::read().mode();
    const bool is_flushed = mode.ftz == ::foundation::core::FpFtz::FlushToZero
                         || mode.denormal_input == ::foundation::core::FpDenormalInput::DenormalsAreZero;
    if (is_flushed) bits |= crucible::EagerNumerics::FLUSH_DENORMAL;
    return ::fixy::Option<crucible::EagerNumerics>::some(crucible::EagerNumerics{.bits = bits});
}

// The read of a region entry.  A dispatch reports a region entry once for
// each iteration of a region, so the read is cold, and the body stays out of
// the record path of each op.  An error of PyTorch in the read unwinds to the
// dispatcher of PyTorch, which gives it to Python.  The attach refuses a
// precision that the vessel does not name (crucible_dispatch_eager_numerics),
// so such a precision here was set after the attach.  The region would then
// fold a word that does not tell its numerics, so the process ends.
[[gnu::cold, gnu::noinline]] inline void observe_eager_numerics(crucible::Vigil& vigil,
                                                                crucible::VigilFgCtx const& fg) {
    read_eager_numerics().match(
        [&vigil, &fg](crucible::EagerNumerics eager) noexcept { vigil.observe_eager_numerics(fg, eager); },
        [] noexcept {
            ::fixy::fatal("the context of PyTorch gives a float32 matmul precision that the vessel does "
                          "not name");
        });
}

}  // namespace crucible::vessel
