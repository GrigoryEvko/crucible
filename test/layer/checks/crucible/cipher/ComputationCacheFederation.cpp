// The compile-time checks of crucible/cipher/ComputationCacheFederation.h.

#include <crucible/cipher/ComputationCacheFederation.h>

namespace crucible::cipher::federation {

namespace detail::computation_cache_federation_self_test {

inline void probe_binary(int, double) noexcept {}
inline void probe_void() noexcept {}

namespace eff_local = ::foundation::effects;
using IOR = eff_local::Row<eff_local::Effect::IO>;
using BgIOR = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;

// The key of the federation of the checks: 32 zero bytes.
inline constexpr ::fixy::FixedArray<std::byte, FEDERATION_DIGEST_BYTES> check_key_bytes{};
inline constexpr FederationHashKey check_key{check_key_bytes.view()};

static_assert(!federation_key<&probe_unary, EmptyR, int>(check_key).is_zero(),
              "the federation key must not be the zero key.");
static_assert(!federation_key<&probe_unary, EmptyR, int>(check_key).is_sentinel(),
              "the federation key must not be the sentinel that marks an empty cache slot.");
static_assert(::fixy::session::is_well_formed_v<ComputationCacheFederationSenderProto<&probe_unary, EmptyR, int>>);
static_assert(::fixy::session::is_well_formed_v<ComputationCacheFederationReceiverProto<&probe_unary, EmptyR, int>>);
static_assert(::fixy::session::is_well_formed_v<ComputationCacheFederationCoordProto<&probe_unary, EmptyR, int>>);
static_assert(role_protocol_matches_v<SenderRole, ComputationCacheFederationSenderProto<&probe_unary, EmptyR, int>,
                                      ComputationCacheFederationKeyTag<&probe_unary, EmptyR, int>>);
static_assert(::fixy::session::is_content_addressed_v<
              typename ComputationCacheFederationContentAddressedPayload<&probe_unary, EmptyR, int>::payload_type>);
static_assert(sizeof(ComputationCacheFederationContentAddressedPayload<&probe_unary, EmptyR, int>)
              == sizeof(::fixy::View<std::byte const>));

static_assert(federation_key<&probe_unary, EmptyR, int>(check_key)
                  == federation_key<&probe_unary, EmptyR, int>(check_key),
              "the federation key must be deterministic for the same inputs.");

static_assert(federation_key<&probe_unary, EmptyR, int>(check_key) != federation_key<&probe_unary, BgR, int>(check_key),
              "the federation key must differ across rows.");
static_assert(federation_key<&probe_unary, BgR, int>(check_key) != federation_key<&probe_unary, IOR, int>(check_key),
              "the federation key distinguishes Row<Bg> from Row<IO>.");
static_assert(federation_key<&probe_unary, BgR, int>(check_key) != federation_key<&probe_unary, BgIOR, int>(check_key),
              "the federation key distinguishes Row<Bg> from Row<Bg, IO>.");

static_assert(federation_key<&probe_unary, EmptyR, int>(check_key) != federation_key<&probe_void, EmptyR>(check_key),
              "the federation key distinguishes different functions.");

static_assert(federation_key<&probe_unary, EmptyR, int>(check_key)
                  != federation_key<&probe_binary, EmptyR, int, double>(check_key),
              "the federation key distinguishes different argument packs.");

using BgIO_perm1 = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;
using BgIO_perm2 = eff_local::Row<eff_local::Effect::IO, eff_local::Effect::Bg>;
using BgIO_repeat = eff_local::Row<eff_local::Effect::IO, eff_local::Effect::Bg, eff_local::Effect::IO>;
static_assert(federation_key<&probe_unary, BgIO_perm1, int>(check_key)
                  == federation_key<&probe_unary, BgIO_perm2, int>(check_key),
              "the federation key does not change when the effect pack is reordered.");
static_assert(federation_key<&probe_unary, BgIO_perm1, int>(check_key)
                  == federation_key<&probe_unary, BgIO_repeat, int>(check_key),
              "the federation key does not change when an effect of the pack repeats.");

static_assert(IsCacheableFunction<&probe_unary>);
static_assert(IsEffectRow<EmptyR>);
static_assert(IsEffectRow<BgR>);
static_assert(IsEffectRow<BgIOR>);

static_assert(ArgsCanonicallyOrdered<>, "an empty argument pack is vacuously canonical.");
static_assert(ArgsCanonicallyOrdered<int>, "a bare payload type is vacuously canonical.");
static_assert(ArgsCanonicallyOrdered<int, double>, "a pack of bare payload types is vacuously canonical.");

using FromUser = ::fixy::tags::source::FromUser;

static_assert(ArgsCanonicallyOrdered<::fixy::Stale<::fixy::Tagged<int, FromUser>>>,
              "Stale outside Tagged is the canonical nesting order and must be "
              "accepted as an argument.");

static_assert(!ArgsCanonicallyOrdered<::fixy::Tagged<::fixy::Stale<int>, FromUser>>,
              "Tagged outside Stale inverts the canonical nesting order and "
              "must be refused at the publish boundary.");

static_assert(!federation_key<&probe_unary, EmptyR, ::fixy::Stale<::fixy::Tagged<int, FromUser>>>(check_key).is_zero(),
              "a canonically nested argument projects to a well-formed federation "
              "key.");

}  // namespace detail::computation_cache_federation_self_test

}  // namespace crucible::cipher::federation
