// The compile-time checks of crucible/ledger/HostFingerprint.h.

#include <crucible/ledger/HostFingerprint.h>

namespace crucible::ledger {

static_assert(sizeof(HardwareDigest) == sizeof(std::uint64_t));
static_assert(sizeof(PolicyDigest) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<HostFingerprint>);

static_assert(std::is_trivially_copyable_v<HostFacts>);

namespace fingerprint_detail::self_test {

// A probe reads files, so the context must admit IO and Block.  The
// startup context admits IO without Block, and the foreground admits
// nothing.
static_assert(CtxFitsHostProbe<::fixy::BgLoadCtx> && CtxFitsHostProbe<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsHostProbe<::fixy::ColdInitCtx> && !CtxFitsHostProbe<::fixy::HotFgCtx>);
static_assert(!CtxFitsHostProbe<int>);

inline constexpr HostFingerprint s_unset{};
static_assert(!s_unset.is_complete());
static_assert(compare_fingerprints(s_unset, s_unset) == FingerprintMatch::Incomplete);

inline constexpr HostFingerprint s_left{HardwareDigest{7u}, PolicyDigest{11u}};
inline constexpr HostFingerprint s_same_hardware{HardwareDigest{7u}, PolicyDigest{13u}};
inline constexpr HostFingerprint s_other{HardwareDigest{9u}, PolicyDigest{11u}};
static_assert(s_left.is_complete());
static_assert(compare_fingerprints(s_left, s_left) == FingerprintMatch::Exact);
static_assert(compare_fingerprints(s_left, s_same_hardware) == FingerprintMatch::PolicyChanged);
static_assert(compare_fingerprints(s_left, s_other) == FingerprintMatch::HardwareChanged);

// A hardware change outranks a policy change: the answer a caller acts on
// must be the more destructive of the two.
inline constexpr HostFingerprint s_both{HardwareDigest{9u}, PolicyDigest{13u}};
static_assert(compare_fingerprints(s_left, s_both) == FingerprintMatch::HardwareChanged);

static_assert(fold_bytes("") != fold_bytes("performance"));
static_assert(fold_bytes("performance") != fold_bytes("powersave"));

// One mix() call is XOR-symmetric, so it cannot distinguish its two
// arguments on its own. The ordering guarantee lives in the CHAIN: each
// step finalizes before the next contribution is folded, so swapping two
// fields changes the digest. Every fold in HostFingerprint.h is written as
// such a chain for exactly that reason, and this pair of assertions is what
// stops someone flattening one into a single xor-then-mix.
static_assert(mix(1u, 2u) == mix(2u, 1u), "a single mix is symmetric — do not rely on it for field ordering");
static_assert(mix(mix(0u, 1u), 2u) != mix(mix(0u, 2u), 1u), "the chained fold must be order-sensitive");

}  // namespace fingerprint_detail::self_test

}  // namespace crucible::ledger
