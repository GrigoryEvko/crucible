// A kernel context comes only from mint_kernel_context, which reads the
// epoch of a live FP scope.  The constructor of the source of a kernel
// context is private, and its one friend is the context, so no code builds a
// context whose mode no register holds.

#include <foundation/core/FpMode.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

namespace {

struct KernelBrand {};

using Source = ::foundation::effects::ctx_cap::KernelSource<KernelBrand, ::foundation::core::kFpModeIeee>;

[[maybe_unused]] void attempt() { [[maybe_unused]] Source const source{std::uint64_t{1}, std::uintptr_t{1}}; }

}  // namespace

int main() { return 0; }
