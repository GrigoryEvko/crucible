// The door of foundation/os/WideAtomic.h: the start-up hook that reads if an
// aligned vector move of 16 bytes is one access on the cores of the host
// (the load and the store on x86-64 of the header).
//
// The hook reads CPUID through foundation/os/Cpuid.h, with the facts of
// libatomic/config/x86/init.c of GCC 16: the bits AVX and CMPXCHG16B of leaf
// 1, and the vendor of leaf 0.  On aarch64 the file declares nothing.  The
// file includes no system header.

#include <foundation/os/WideAtomic.h>

#include <foundation/os/Cpuid.h>

#include <cstdint>

#if defined(__x86_64__)

namespace foundation::os {

namespace detail {

// The record of the start-up hook.  One copy in each library is correct: the
// hook of a copy writes it one time, at the load of its library, and only the
// cells of that library read it.  Each read comes after the write, because
// the thread of the read starts after main starts, or it reaches the library
// through the dlopen that ran the write.
constinit bool has_atomic_vector_moves_ = false;

}  // namespace detail

namespace {

// The bits of ECX of CPUID leaf 1.
constexpr std::uint32_t kCmpxchg16bBit = std::uint32_t{1} << 13;
constexpr std::uint32_t kAvxBit = std::uint32_t{1} << 28;

// The word ECX of the vendor text of leaf 0 of each vendor that states the
// fact: "GenuineIntel", "AuthenticAMD", "CentaurHauls" and "  Shanghai  ".
constexpr std::uint32_t kIntelEcx = 0x6C65746EU;
constexpr std::uint32_t kAmdEcx = 0x444D4163U;
constexpr std::uint32_t kCentaurEcx = 0x736C7561U;
constexpr std::uint32_t kShanghaiEcx = 0x20206961U;

// Centaur states the fact only for the cores above family 6.
constexpr std::uint32_t kLastCentaurFamilyWithNoFact = 6;

// True when the core has AVX and CMPXCHG16B, and its vendor states that an
// aligned vector move of 16 bytes is one access.
[[nodiscard]] bool read_atomic_vector_moves() noexcept {
    CpuidWords const vendor = read_cpuid(0, 0);
    if (vendor.eax < 1) return false;
    CpuidWords const model = read_cpuid(1, 0);
    bool const has_features = (model.ecx & (kAvxBit | kCmpxchg16bBit)) == (kAvxBit | kCmpxchg16bBit);
    if (!has_features) return false;
    std::uint32_t const family = (model.eax >> 8) & 0x0FU;
    bool const is_stated_centaur = vendor.ecx == kCentaurEcx && family > kLastCentaurFamilyWithNoFact;
    return vendor.ecx == kIntelEcx || vendor.ecx == kAmdEcx || vendor.ecx == kShanghaiEcx || is_stated_centaur;
}

// The hook of the record.  The priority 104 comes after the start-up check of
// the kernel floor (101), and before each constructor with no priority.
[[gnu::constructor(104)]] void read_vector_moves_at_start() noexcept {
    detail::has_atomic_vector_moves_ = read_atomic_vector_moves();
}

}  // namespace

}  // namespace foundation::os

#endif
