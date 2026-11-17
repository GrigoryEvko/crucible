#pragma once

// The CPUID door of the base: one read of a leaf of the CPUID instruction of
// x86_64.
//
// CPUID gives the features of the core and the rate of its time-stamp
// counter.  The spin-hint door of foundation/os/SpinHint.h reads the wait
// features, and the spin budget of fixy/os/SpinBudget.h reads the vendor, the
// hypervisor bit and the rate of the counter.  The level door of
// foundation/os/X86Level.h reads the features of each x86-64 level, and the
// word XCR0 of XGETBV, which tells the register states that the OS saves.
// The two instructions need no system header and no system call.  On aarch64
// the header declares nothing, and the auxiliary vector of
// foundation/os/Auxiliary.h gives the features.
//
// This header is in the layer F0_doors of utils/rule-table.txt.

#include <cstdint>

namespace foundation::os {

#if defined(__x86_64__)

// The four words that CPUID gives for one leaf and one subleaf.
struct CpuidWords {
    std::uint32_t eax = 0;
    std::uint32_t ebx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t edx = 0;
};

// The words of the leaf LEAF and the subleaf SUBLEAF.  A leaf above the
// largest leaf of the core gives the words of the largest basic leaf, so a
// caller reads the largest leaf first (the word EAX of leaf 0, or of leaf
// 0x80000000 for the extended leaves).
[[nodiscard]] inline CpuidWords read_cpuid(std::uint32_t leaf, std::uint32_t subleaf) noexcept {
    CpuidWords words{};
    asm volatile("cpuid"
                 : "=a"(words.eax), "=b"(words.ebx), "=c"(words.ecx), "=d"(words.edx)
                 : "a"(leaf), "c"(subleaf));
    return words;
}

// The word of the extended control register XCR0, which XGETBV gives.  Each
// bit of XCR0 names a state of the registers that the OS saves at a switch of
// the context.  The instruction exists only when CPUID sets the bit OSXSAVE
// (bit 27 of the word ECX of leaf 1), so a caller reads that bit first.  The
// asm names the instruction, so the unit needs no -mxsave.
[[nodiscard]] inline std::uint64_t read_xcr0() noexcept {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    asm volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0U));
    return (std::uint64_t{high} << 32) | low;
}

#endif

}  // namespace foundation::os
