// A kernel context comes only from a live FP scope.  A word of the FP
// control register is no scope: it holds no epoch, and the register can hold
// another mode while the word lives.  So the mint of a kernel context refuses
// a word.

#include <fixy/Ctx.h>
#include <foundation/os/FpControl.h>

namespace {

struct KernelBrand {};

[[maybe_unused]] void attempt() {
    ::foundation::os::FpControlWord const word = ::foundation::os::FpControlWord::read();
    [[maybe_unused]] auto ctx = ::fixy::mint_kernel_context<KernelBrand>(word);
}

}  // namespace

int main() { return 0; }
