#pragma once

#include <crucible/Platform.h>

namespace crucible::safety::witness::arch {

struct X86_64 final {};
struct AArch64 final {};
struct RISCV final {};

static_assert(sizeof(X86_64) == 1);
static_assert(sizeof(AArch64) == 1);
static_assert(sizeof(RISCV) == 1);

#if defined(__x86_64__)
using current_arch_tag = X86_64;
#elif defined(__aarch64__)
using current_arch_tag = AArch64;
#elif defined(__riscv)
using current_arch_tag = RISCV;
#else
#error "crucible::safety::witness: unsupported architecture. The supported tags are X86_64, AArch64 and RISCV."
#endif

}  // namespace crucible::safety::witness::arch
