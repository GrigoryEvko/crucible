// The compile-time checks of crucible/cipher/ComputationCacheFederation.h.

#include <crucible/cipher/ComputationCacheFederation.h>

namespace crucible::cipher::federation {

namespace detail::computation_cache_federation_self_test {

inline void probe_binary(int, double) noexcept {}
inline void probe_void() noexcept {}

namespace eff_local = ::foundation::effects;
using IOR = eff_local::Row<eff_local::Effect::IO>;
using BgIOR = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;

static_assert(federation_content_hash<&probe_unary, EmptyR, int>().raw() != 0,
              "the federation content hash must be non-zero, which the "
              "non-zero name seed guarantees.");
static_assert(federation_row_hash<EmptyR>().raw() != 0, "the federation row hash for the empty row must be non-zero, "
                                                        "which the cardinality-seeded fold guarantees.");
static_assert(!federation_key<&probe_unary, EmptyR, int>().is_zero(),
              "the composite federation key must not be the zero-key sentinel.");
static_assert(!federation_key<&probe_unary, EmptyR, int>().is_sentinel(),
              "the composite federation key must not be the all-ones pair "
              "that marks an empty cache slot.");
static_assert(::fixy::session::is_well_formed_v<ComputationCacheFederationSenderProto<&probe_unary, EmptyR, int>>);
static_assert(::fixy::session::is_well_formed_v<ComputationCacheFederationReceiverProto<&probe_unary, EmptyR, int>>);
static_assert(::fixy::session::is_well_formed_v<ComputationCacheFederationCoordProto<&probe_unary, EmptyR, int>>);
static_assert(role_protocol_matches_v<SenderRole, ComputationCacheFederationSenderProto<&probe_unary, EmptyR, int>,
                                      ComputationCacheFederationKeyTag<&probe_unary, EmptyR, int>>);
static_assert(::fixy::session::is_content_addressed_v<
              typename ComputationCacheFederationContentAddressedPayload<&probe_unary, EmptyR, int>::payload_type>);
static_assert(sizeof(ComputationCacheFederationContentAddressedPayload<&probe_unary, EmptyR, int>)
              == sizeof(std::span<const std::uint8_t>));

static_assert(federation_key<&probe_unary, EmptyR, int>() == federation_key<&probe_unary, EmptyR, int>(),
              "the federation key must be deterministic for the same inputs.");

static_assert(federation_key<&probe_unary, EmptyR, int>() != federation_key<&probe_unary, BgR, int>(),
              "the federation key must differ across rows, which is what "
              "makes the row axis able to tell entries apart.");

static_assert(federation_row_hash<EmptyR>() != federation_row_hash<BgR>(),
              "the row hash distinguishes the empty row from Row<Bg>.");
static_assert(federation_row_hash<BgR>() != federation_row_hash<IOR>(),
              "the row hash distinguishes Row<Bg> from Row<IO>.");
static_assert(federation_row_hash<BgR>() != federation_row_hash<BgIOR>(),
              "the row hash distinguishes Row<Bg> from Row<Bg, IO>.");

static_assert(federation_key<&probe_unary, EmptyR, int>() != federation_key<&probe_void, EmptyR>(),
              "the federation key distinguishes different functions.");

static_assert(federation_key<&probe_unary, EmptyR, int>() != federation_key<&probe_binary, EmptyR, int, double>(),
              "the federation key distinguishes different argument packs.");

using BgIO_perm1 = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;
using BgIO_perm2 = eff_local::Row<eff_local::Effect::IO, eff_local::Effect::Bg>;
static_assert(federation_row_hash<BgIO_perm1>() == federation_row_hash<BgIO_perm2>(),
              "the row hash does not change when the effect pack is reordered.");
static_assert(federation_key<&probe_unary, BgIO_perm1, int>() == federation_key<&probe_unary, BgIO_perm2, int>(),
              "the composite federation key does not change when the effect "
              "pack is reordered.");

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

static_assert(!federation_key<&probe_unary, EmptyR, ::fixy::Stale<::fixy::Tagged<int, FromUser>>>().is_zero(),
              "a canonically nested argument projects to a well-formed federation "
              "key.");

}  // namespace detail::computation_cache_federation_self_test

}  // namespace crucible::cipher::federation
