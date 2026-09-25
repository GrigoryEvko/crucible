// Including the header here puts its static_asserts and smoke-test
// assertions into the build graph.

#include <crucible/fixy/Fn.h>

#include <type_traits>
#include <utility>

namespace crucible_fixy = crucible::fixy;
namespace gr = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

static_assert(
    noexcept(crucible_fixy::mint_fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                           strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                           strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>,
                           strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
                           strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
                           strict<D::Synchronization>, strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>,
                           strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>,
                           strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>,
                           strict<D::MemoryScope>>(42)),
    "mint_fn<int, ...>(int) must be noexcept (int is "
    "nothrow-move-constructible).");

static_assert(std::is_same_v<decltype(std::declval<crucible_fixy::stance::PureLinear<int>&>().value()), int&>,
              "crucible_fixy::fn<T>::value() on lvalue must return T&.");

static_assert(std::is_same_v<decltype(std::declval<const crucible_fixy::stance::PureLinear<int>&>().value()), const int&>,
              "crucible_fixy::fn<T>::value() on const lvalue must return const T&.");

static_assert(std::is_same_v<decltype(std::declval<crucible_fixy::stance::PureLinear<int>&&>().value()), int&&>,
              "crucible_fixy::fn<T>::value() on rvalue must return T&&.");

static_assert(std::is_same_v<typename crucible_fixy::stance::PureLinear<int>::safety_fn_t, crucible::safety::fn::Fn<int>>,
              "stance::PureLinear<int>::safety_fn_t must round-trip to "
              "safety::fn::Fn<int>'s all-default instantiation.");

static_assert(std::is_same_v<typename crucible_fixy::stance::IoFunction<int>::effect_row_t,
                             crucible::effects::Row<crucible::effects::Effect::IO>>,
              "stance::IoFunction's Effect row must contain exactly Effect::IO.");

static_assert(crucible_fixy::stance::PureCopy<int>::usage_v == crucible::safety::fn::UsageMode::Copy,
              "stance::PureCopy must resolve Usage to UsageMode::Copy.");

static_assert(crucible_fixy::stance::AsyncEndpoint<int>::reentrancy_v == crucible::safety::fn::ReentrancyMode::Coroutine,
              "stance::AsyncEndpoint must resolve Reentrancy to Coroutine.");

static_assert(std::is_same_v<typename crucible_fixy::stance::BgWorker<int>::effect_row_t,
                             crucible::effects::Row<crucible::effects::Effect::Bg, crucible::effects::Effect::Alloc>>,
              "stance::BgWorker's Effect row must contain Bg + Alloc.");

// declassify requires the policy to derive from secret_policy_base.
namespace policy_tags {
struct AuditTrailPolicy final : ::crucible::safety::secret_policy::secret_policy_base {};
struct InternalLeakPolicy final : ::crucible::safety::secret_policy::secret_policy_base {};
}  // namespace policy_tags

static_assert(std::is_same_v<typename crucible_fixy::stance::PureLinear<int>::policy_t, void>,
              "PureLinear has no declassify grant — policy_t must be void.");

using fn_with_audit_policy = crucible_fixy::fn<
    int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::declassify<policy_tags::AuditTrailPolicy>,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>, strict<D::Space>, strict<D::Overflow>,
    strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
    strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
    strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;

static_assert(std::is_same_v<typename fn_with_audit_policy::policy_t, policy_tags::AuditTrailPolicy>,
              "declassify<AuditTrailPolicy> must surface AuditTrailPolicy via policy_t.");
static_assert(fn_with_audit_policy::security_v == crucible::safety::fn::SecLevel::Public,
              "declassify still resolves security_v to Public regardless of Policy.");

namespace b3_policy_tags {
struct EmitPolicy final : ::crucible::safety::secret_policy::secret_policy_base {};
}  // namespace b3_policy_tags

static_assert(crucible_fixy::stance::CtCrypto<int>::security_v == crucible::safety::fn::SecLevel::Secret,
              "stance::CtCrypto must resolve Security to Secret via as_secret.");

static_assert(std::is_same_v<typename crucible_fixy::stance::CtCrypto<int>::effect_row_t, crucible::effects::Row<>>,
              "stance::CtCrypto's Effect row must be empty (no IO).");

static_assert(std::is_same_v<typename crucible_fixy::stance::CtCrypto<int>::policy_t, void>,
              "stance::CtCrypto must have no declassify policy (policy_t == void).");

static_assert(crucible_fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>::security_v
                  == crucible::safety::fn::SecLevel::Public,
              "stance::PublicEmit must resolve Security to Public via declassify.");

static_assert(std::is_same_v<typename crucible_fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>::effect_row_t,
                             crucible::effects::Row<crucible::effects::Effect::IO>>,
              "stance::PublicEmit's Effect row must contain IO.");

static_assert(std::is_same_v<typename crucible_fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>::policy_t,
                             b3_policy_tags::EmitPolicy>,
              "stance::PublicEmit must surface the Policy tag via policy_t.");

// Every SecLevel enumerator must be reachable through at least one
// stance.  Classified is the strict-default arm, reached by any stance
// that engages no Security grant.

static_assert(crucible_fixy::stance::InternalApi<int>::security_v == crucible::safety::fn::SecLevel::Internal,
              "stance::InternalApi must resolve Security to Internal via "
              "grant::as_internal.");

static_assert(crucible_fixy::stance::UnclassifiedScratch<int>::security_v == crucible::safety::fn::SecLevel::Unclassified,
              "stance::UnclassifiedScratch must resolve Security to "
              "Unclassified via grant::as_unclassified.");

static_assert(std::is_same_v<typename crucible_fixy::stance::InternalApi<int>::effect_row_t, crucible::effects::Row<>>,
              "stance::InternalApi must have an empty Effect row.");
static_assert(std::is_same_v<typename crucible_fixy::stance::InternalApi<int>::policy_t, void>,
              "stance::InternalApi must have no declassify policy, so policy_t "
              "is void.");

static_assert(std::is_same_v<typename crucible_fixy::stance::UnclassifiedScratch<int>::effect_row_t, crucible::effects::Row<>>,
              "stance::UnclassifiedScratch must have an empty Effect row.");
static_assert(std::is_same_v<typename crucible_fixy::stance::UnclassifiedScratch<int>::policy_t, void>,
              "stance::UnclassifiedScratch must have no declassify policy, so "
              "policy_t is void.");

// A new enumerator must add a carrier stance and extend this list.
static_assert(crucible_fixy::stance::UnclassifiedScratch<int>::security_v == crucible::safety::fn::SecLevel::Unclassified
                  && crucible_fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>::security_v
                         == crucible::safety::fn::SecLevel::Public
                  && crucible_fixy::stance::InternalApi<int>::security_v == crucible::safety::fn::SecLevel::Internal
                  && crucible_fixy::stance::PureLinear<int>::security_v == crucible::safety::fn::SecLevel::Classified
                  && crucible_fixy::stance::CtCrypto<int>::security_v == crucible::safety::fn::SecLevel::Secret,
              "Every SecLevel enumerator must be reachable through at least one "
              "stance.");

static_assert(sizeof(crucible_fixy::stance::InternalApi<int>) == sizeof(int));
static_assert(sizeof(crucible_fixy::stance::UnclassifiedScratch<int>) == sizeof(int));

// The Trust default is the bottom of the integrity lattice, not the
// top.  Defaulting to the top would let every unannotated binding
// claim maximum integrity for free.  Verified is earned by engaging
// the grant explicitly, which keeps the verification surface greppable.

static_assert(std::is_same_v<typename crucible::safety::fn::Fn<int>::trust_t, crucible::safety::trust::Unverified>,
              "safety::fn::Fn<int> with no explicit Trust must default to "
              "safety::trust::Unverified, the bottom of the integrity lattice.  "
              "Defaulting to trust::Verified turns the lattice upside down.");

static_assert(
    std::is_same_v<typename crucible_fixy::stance::PureLinear<int>::safety_fn_t::trust_t, crucible::safety::trust::Unverified>,
    "stance::PureLinear is strict on every axis, so it must resolve "
    "Trust to Unverified, mirroring the substrate default.  A failure "
    "here means strict_default_for<Trust> and the substrate "
    "Fn<>::Trust default have drifted apart.");

static_assert(std::is_same_v<typename crucible::safety::fn::Fn<
                                 int, crucible::safety::fn::pred::True, crucible::safety::fn::UsageMode::Linear,
                                 crucible::effects::Row<>, crucible::safety::fn::SecLevel::Classified,
                                 crucible::safety::fn::proto::None, crucible::safety::fn::lifetime::Static,
                                 crucible::safety::source::FromInternal, crucible::safety::trust::Verified>::trust_t,
                             crucible::safety::trust::Verified>,
              "An explicit substrate Fn<..., trust::Verified> must resolve "
              "trust_t to safety::trust::Verified.  Earning Verified is the "
              "canonical opt-in path.");

static_assert(!std::is_same_v<crucible::safety::trust::Verified, crucible::safety::trust::Unverified>,
              "Verified and Unverified must remain distinct types.  Aliasing "
              "them collapses the lattice and defeats the fail-safe default.");

static_assert(sizeof(crucible_fixy::stance::PureLinear<int>) == sizeof(int));
static_assert(sizeof(crucible_fixy::stance::PureLinear<char>) == sizeof(char));
static_assert(sizeof(crucible_fixy::stance::PureLinear<double>) == sizeof(double));
static_assert(sizeof(crucible_fixy::stance::IoFunction<int>) == sizeof(int));
static_assert(sizeof(crucible_fixy::stance::PureCopy<int>) == sizeof(int));
static_assert(sizeof(crucible_fixy::stance::BgWorker<int>) == sizeof(int));
static_assert(sizeof(crucible_fixy::stance::AsyncEndpoint<int>) == sizeof(int));
static_assert(sizeof(crucible_fixy::stance::CtCrypto<int>) == sizeof(int));
static_assert(sizeof(crucible_fixy::stance::PublicEmit<int, b3_policy_tags::EmitPolicy>) == sizeof(int));

static_assert(std::is_same_v<decltype(crucible_fixy::mint_fn_for<crucible_fixy::stance::PureLinear>(42)), crucible_fixy::stance::PureLinear<int>>,
              "mint_fn_for<PureLinear>(int) must deduce Type=int and return "
              "the stance instantiation.");

static_assert(std::is_same_v<decltype(crucible_fixy::mint_fn_for<crucible_fixy::stance::PureCopy>('a')), crucible_fixy::stance::PureCopy<char>>,
              "mint_fn_for<PureCopy>(char) must deduce Type=char.");

// The binary overload takes the Policy explicitly and deduces Type,
// which the unary form cannot do for a two-parameter stance.
static_assert(std::is_same_v<decltype(crucible_fixy::mint_fn_for<crucible_fixy::stance::SecretConsumer, b3_policy_tags::EmitPolicy>(42)),
                             crucible_fixy::stance::SecretConsumer<int, b3_policy_tags::EmitPolicy>>,
              "mint_fn_for<SecretConsumer, Policy>(int) must deduce Type=int and "
              "return SecretConsumer<int, Policy>.");

static_assert(std::is_same_v<decltype(crucible_fixy::mint_fn_for<crucible_fixy::stance::PublicEmit, b3_policy_tags::EmitPolicy>('z')),
                             crucible_fixy::stance::PublicEmit<char, b3_policy_tags::EmitPolicy>>,
              "mint_fn_for<PublicEmit, Policy>(char) must deduce Type=char and "
              "return PublicEmit<char, Policy>.");

int main() {
    auto v1 =
        crucible_fixy::mint_fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>(
            7);
    if (v1.value() != 7) return 1;

    // The value constructor is private, so a stance instance can only
    // come from the factory.
    auto v2 = crucible_fixy::mint_fn_for<crucible_fixy::stance::PureLinear>(11);
    if (v2.value() != 11) return 2;

    auto v3 = crucible_fixy::mint_fn_for<crucible_fixy::stance::IoFunction>(13);
    if (v3.value() != 13) return 3;

    auto v4 = crucible_fixy::mint_fn_for<crucible_fixy::stance::PureCopy>(17);
    if (v4.value() != 17) return 4;

    auto v5 = crucible_fixy::mint_fn_for<crucible_fixy::stance::PureLinear>(23);
    if (v5.value() != 23) return 5;

    return 0;
}
