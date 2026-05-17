// ── test_fixy_fn — Phase B sentinel for the universal integration ─
//
// Pulls fixy/Fn.h into a TU compiled under project warning flags so
// the header's static_asserts + smoke-test assertions execute.  Adds
// a small set of runtime witnesses on top of the header-embedded
// claims:
//
//   1. mint_fn round-trip — minted value equals seeded value
//   2. stance::PureLinear value access — value() deduces correctly
//      under const + rvalue self
//   3. safety_fn_t round-trip — fixy::fn<int>::safety_fn_t IS
//      safety::fn::Fn<int> (header asserts statically; runtime check
//      pins the name)
//   4. stance::IoFunction Effect row contains IO
//   5. stance::PureCopy resolves Usage to Copy
//
// Per HS14, this is the positive-compile sentinel; the matching
// neg-compile fixtures land under test/fixy_neg/neg_fixy_rule_*.cpp
// in the next task (FIXY-CLEAN-B4).

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Rules.h>

#include <type_traits>
#include <utility>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

// ─── 1. mint_fn round-trip ─────────────────────────────────────────

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

static_assert(noexcept(fixy::mint_fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
    strict<D::Staleness>>(42)),
    "mint_fn<int, ...>(int) must be noexcept (int is "
    "nothrow-move-constructible).");

// ─── 2. value() deduces correctly under const and rvalue ──────────

static_assert(std::is_same_v<
    decltype(std::declval<fixy::stance::PureLinear<int>&>().value()),
    int&>,
    "fixy::fn<T>::value() on lvalue must return T&.");

static_assert(std::is_same_v<
    decltype(std::declval<const fixy::stance::PureLinear<int>&>().value()),
    const int&>,
    "fixy::fn<T>::value() on const lvalue must return const T&.");

static_assert(std::is_same_v<
    decltype(std::declval<fixy::stance::PureLinear<int>&&>().value()),
    int&&>,
    "fixy::fn<T>::value() on rvalue must return T&&.");

// ─── 3. safety_fn_t round-trip — fixy::fn<int>::safety_fn_t ───────
//   IS the all-default safety::fn::Fn<int>.

static_assert(std::is_same_v<
    typename fixy::stance::PureLinear<int>::safety_fn_t,
    crucible::safety::fn::Fn<int>>,
    "stance::PureLinear<int>::safety_fn_t must round-trip to "
    "safety::fn::Fn<int>'s all-default instantiation.");

// ─── 4. stance::IoFunction Effect row contains IO ─────────────────

static_assert(std::is_same_v<
    typename fixy::stance::IoFunction<int>::effect_row_t,
    crucible::effects::Row<crucible::effects::Effect::IO>>,
    "stance::IoFunction's Effect row must contain exactly Effect::IO.");

// ─── 5. stance::PureCopy resolves Usage to Copy ───────────────────

static_assert(fixy::stance::PureCopy<int>::usage_v
    == crucible::safety::fn::UsageMode::Copy,
    "stance::PureCopy must resolve Usage to UsageMode::Copy.");

// ─── 6. stance::AsyncEndpoint resolves Reentrancy to Coroutine ────

static_assert(fixy::stance::AsyncEndpoint<int>::reentrancy_v
    == crucible::safety::fn::ReentrancyMode::Coroutine,
    "stance::AsyncEndpoint must resolve Reentrancy to Coroutine.");

// ─── 7. BgWorker Effect row contains both Bg and Alloc ────────────

static_assert(std::is_same_v<
    typename fixy::stance::BgWorker<int>::effect_row_t,
    crucible::effects::Row<crucible::effects::Effect::Bg,
                           crucible::effects::Effect::Alloc>>,
    "stance::BgWorker's Effect row must contain Bg + Alloc.");

// ─── 7b. fixy::rule:: alias smoke — R001..R020 substrate bijection
//        already verified inside Rules.h.  Here we just witness that
//        the alias names resolve and are distinct from one another.

static_assert(!std::is_same_v<fixy::rule::R001, fixy::rule::R002>);
static_assert(!std::is_same_v<fixy::rule::R013, fixy::rule::R017>);
static_assert(!std::is_same_v<fixy::rule::R019, fixy::rule::R020>);

// ─── 7c. declassify<Policy> policy_t projection (FIXY-AUDIT-A2) ───
//
// A binding that omits declassify resolves policy_t to `void`; a
// binding with declassify<P> exposes P via fn<...>::policy_t.

namespace policy_tags {
struct AuditTrailPolicy {};
struct InternalLeakPolicy {};
}  // namespace policy_tags

// No declassify grant → policy_t == void.
static_assert(std::is_same_v<
    typename fixy::stance::PureLinear<int>::policy_t, void>,
    "PureLinear has no declassify grant — policy_t must be void.");

// With declassify<AuditTrailPolicy> → policy_t exposes the tag.
using fn_with_audit_policy = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    gr::declassify<policy_tags::AuditTrailPolicy>,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>>;

static_assert(std::is_same_v<
    typename fn_with_audit_policy::policy_t,
    policy_tags::AuditTrailPolicy>,
    "declassify<AuditTrailPolicy> must surface AuditTrailPolicy via policy_t.");
static_assert(fn_with_audit_policy::security_v
    == crucible::safety::fn::SecLevel::Public,
    "declassify still resolves security_v to Public regardless of Policy.");

// ─── 7d. CtCrypto + PublicEmit stance witnesses (FIXY-AUDIT-B3) ────
//
// CtCrypto: Security pinned to Secret, Effect row empty (no IO), and
// the §30.14 detector does NOT fire because has_io is false.
// PublicEmit<Policy>: Security pinned to Public via declassify,
// Effect=IO, policy_t accessor surfaces the named Policy.

namespace b3_policy_tags {
struct EmitPolicy {};
}  // namespace b3_policy_tags

static_assert(fixy::stance::CtCrypto<int>::security_v
    == crucible::safety::fn::SecLevel::Secret,
    "stance::CtCrypto must resolve Security to Secret via as_secret.");

static_assert(std::is_same_v<
    typename fixy::stance::CtCrypto<int>::effect_row_t,
    crucible::effects::Row<>>,
    "stance::CtCrypto's Effect row must be empty (no IO).");

static_assert(std::is_same_v<
    typename fixy::stance::CtCrypto<int>::policy_t, void>,
    "stance::CtCrypto must have no declassify policy (policy_t == void).");

static_assert(fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>::security_v
    == crucible::safety::fn::SecLevel::Public,
    "stance::PublicEmit must resolve Security to Public via declassify.");

static_assert(std::is_same_v<
    typename fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>::effect_row_t,
    crucible::effects::Row<crucible::effects::Effect::IO>>,
    "stance::PublicEmit's Effect row must contain IO.");

static_assert(std::is_same_v<
    typename fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>::policy_t,
    b3_policy_tags::EmitPolicy>,
    "stance::PublicEmit must surface the Policy tag via policy_t.");

// ─── 8. EBO collapse across multiple types ────────────────────────

static_assert(sizeof(fixy::stance::PureLinear<int>)    == sizeof(int));
static_assert(sizeof(fixy::stance::PureLinear<char>)   == sizeof(char));
static_assert(sizeof(fixy::stance::PureLinear<double>) == sizeof(double));
static_assert(sizeof(fixy::stance::IoFunction<int>)    == sizeof(int));
static_assert(sizeof(fixy::stance::PureCopy<int>)      == sizeof(int));
static_assert(sizeof(fixy::stance::BgWorker<int>)      == sizeof(int));
static_assert(sizeof(fixy::stance::AsyncEndpoint<int>) == sizeof(int));
static_assert(sizeof(fixy::stance::CtCrypto<int>)      == sizeof(int));
static_assert(sizeof(fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>)
              == sizeof(int));

// ─── 8b. mint_fn_for<Stance>(value) — stance-bound convenience ────
//        (FIXY-AUDIT-A11)

static_assert(std::is_same_v<
    decltype(fixy::mint_fn_for<fixy::stance::PureLinear>(42)),
    fixy::stance::PureLinear<int>>,
    "mint_fn_for<PureLinear>(int) must deduce Type=int and return "
    "the stance instantiation.");

static_assert(std::is_same_v<
    decltype(fixy::mint_fn_for<fixy::stance::PureCopy>('a')),
    fixy::stance::PureCopy<char>>,
    "mint_fn_for<PureCopy>(char) must deduce Type=char.");

// fixy-H-01: binary stance overload — Policy explicit, Type deduced.
// Covers stance::SecretConsumer<Type, Policy> + stance::PublicEmit<Type, Policy>
// that the original unary mint_fn_for could NOT instantiate.
static_assert(std::is_same_v<
    decltype(fixy::mint_fn_for<fixy::stance::SecretConsumer,
                               b3_policy_tags::EmitPolicy>(42)),
    fixy::stance::SecretConsumer<int, b3_policy_tags::EmitPolicy>>,
    "mint_fn_for<SecretConsumer, Policy>(int) must deduce Type=int and "
    "return SecretConsumer<int, Policy>.");

static_assert(std::is_same_v<
    decltype(fixy::mint_fn_for<fixy::stance::PublicEmit,
                               b3_policy_tags::EmitPolicy>('z')),
    fixy::stance::PublicEmit<char, b3_policy_tags::EmitPolicy>>,
    "mint_fn_for<PublicEmit, Policy>(char) must deduce Type=char and "
    "return PublicEmit<char, Policy>.");

// ─── 9. Runtime witness — mint_fn produces a fixy::fn carrying the
//        supplied value, and value() retrieves it bit-exactly.

int main() {
    auto v1 = fixy::mint_fn<int,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
        strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>>(7);
    if (v1.value() != 7) return 1;

    // Stance alias smoke
    fixy::stance::PureLinear<int> v2{11};
    if (v2.value() != 11) return 2;

    fixy::stance::IoFunction<int> v3{13};
    if (v3.value() != 13) return 3;

    fixy::stance::PureCopy<int> v4{17};
    if (v4.value() != 17) return 4;

    // mint_fn_for runtime witness
    auto v5 = fixy::mint_fn_for<fixy::stance::PureLinear>(23);
    if (v5.value() != 23) return 5;

    return 0;
}
