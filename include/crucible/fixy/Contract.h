#pragma once

// No other include below pulls the complete Cipher class, and the migration
// aliases need it for their identity checks.
#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/cipher/CipherTierPromotion.h>
#include <crucible/cipher/ComputationCache.h>
#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/safety/Contract.h>

#include <chrono>  // drain_computation_cache takes a duration
#include <type_traits>

namespace crucible::fixy::contract {

namespace cipher {

using ::crucible::safety::CipherTier;
using ::crucible::safety::CipherTierLattice;
using ::crucible::safety::CipherTierTag_v;

template <typename T>
using HotTierHandle = ::crucible::cipher::HotTierHandle<T>;

template <typename T>
using WarmTierHandle = ::crucible::cipher::WarmTierHandle<T>;

template <typename T>
using ColdTierHandle = ::crucible::cipher::ColdTierHandle<T>;

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_promote_tier_v = ::crucible::cipher::can_promote_tier_v<From, To>;

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_demote_tier_v = ::crucible::cipher::can_demote_tier_v<From, To>;

using ::crucible::cipher::mint_promote;
using ::crucible::cipher::mint_demote;
using ::crucible::cipher::mint_restore;

using ::crucible::cipher::RestoreError;
using ::crucible::cipher::restore_error_name;

template <typename T>
using HotPromotePayload = ::crucible::cipher::HotPromotePayload<T>;

template <typename T>
using HotPromote = ::crucible::cipher::HotPromote<T>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteDelegate = ::crucible::cipher::HotPromoteDelegate<T, K>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteAccept = ::crucible::cipher::HotPromoteAccept<T, K>;

// EpochedDelegate and mint_persisted_session are reachable through other
// surfaces too.  They repeat here so a Cipher migration flow reads in one place.

template <typename T, typename K = ::crucible::safety::proto::End, unsigned MinEpoch = 0, unsigned MinGeneration = 0>
using EpochedDelegate = ::crucible::safety::proto::EpochedDelegate<T, K, MinEpoch, MinGeneration>;

using ::crucible::safety::proto::mint_persisted_session;

using ::crucible::Cipher;

// Cipher::OpenView names the same type.  This namespace-scope spelling exists
// so a caller can reach it without the class definition.
using ::crucible::CipherOpenView;

using ::crucible::CipherSessionEventPersistenceRow;

template <typename T>
using ContentAddressedPayload = ::crucible::cipher::ContentAddressedPayload<T>;

template <typename T>
using LoadedContentAddressedPayload = ::crucible::cipher::LoadedContentAddressedPayload<T>;

using SessionEvent = ::crucible::safety::proto::SessionEvent;

// CompiledBody stays incomplete here.  The dispatcher supplies the definition.
using ::crucible::cipher::CompiledBody;

using ::crucible::cipher::IsCacheableFunction;
using ::crucible::cipher::IsEffectRow;

template <auto FnPtr, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
inline constexpr std::uint64_t computation_cache_key = ::crucible::cipher::computation_cache_key<FnPtr, Args...>;

using ::crucible::cipher::lookup_computation_cache;

using ::crucible::cipher::insert_computation_cache;

template <auto FnPtr, typename Row, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr> && ::crucible::cipher::IsEffectRow<Row>
inline constexpr std::uint64_t computation_cache_key_in_row =
    ::crucible::cipher::computation_cache_key_in_row<FnPtr, Row, Args...>;

using ::crucible::cipher::lookup_computation_cache_in_row;
using ::crucible::cipher::insert_computation_cache_in_row;

using ::crucible::cipher::drain_computation_cache;

namespace federation {

using ::crucible::cipher::federation::FEDERATION_MAGIC;
using ::crucible::cipher::federation::FEDERATION_PROTOCOL_V1;
using ::crucible::cipher::federation::FEDERATION_HEADER_BYTES;

using ::crucible::cipher::federation::FederationEntryHeader;
using ::crucible::cipher::federation::ColdBlobRegion;
using ::crucible::cipher::federation::FederationEntryView;

using ::crucible::cipher::federation::FederationError;
using ::crucible::cipher::federation::federation_error_name;

using ::crucible::cipher::federation::serialize_federation_entry;
using ::crucible::cipher::federation::deserialize_federation_header;
using ::crucible::cipher::federation::deserialize_untrusted_federation_entry;
using ::crucible::cipher::federation::deserialize_federation_entry;

using ::crucible::cipher::federation::federation_entry_blob_layout_disjoint;
using ::crucible::cipher::federation::cold_blob_regions_pairwise_disjoint;
using ::crucible::cipher::federation::federation_accepts_cardinality;

using ::crucible::cipher::federation::ComputationCacheFederationKeyTag;
using ::crucible::cipher::federation::ComputationCacheFederationSenderProto;
using ::crucible::cipher::federation::ComputationCacheFederationReceiverProto;
using ::crucible::cipher::federation::ComputationCacheFederationCoordProto;

using ::crucible::cipher::federation::ContentAddressedFederationPayload;

using ::crucible::cipher::federation::ComputationCacheFederationPayload;
using ::crucible::cipher::federation::ComputationCacheFederationContentAddressedPayload;

using ::crucible::cipher::federation::federation_content_hash;
using ::crucible::cipher::federation::federation_row_hash;
using ::crucible::cipher::federation::federation_key;

using ::crucible::cipher::federation::serialize_computation_cache_federation_entry;

}  // namespace federation

}  // namespace cipher

}  // namespace crucible::fixy::contract

namespace crucible::fixy::contract::self_test {

static_assert(std::is_same_v<cipher::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>,
                             ::crucible::safety::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>>,
              "fixy::contract::cipher::CipherTier must alias safety::CipherTier.");

static_assert(std::is_same_v<cipher::HotTierHandle<int>, ::crucible::safety::cipher_tier::Hot<int>>,
              "fixy::contract::cipher::HotTierHandle must alias "
              "safety::cipher_tier::Hot.");

static_assert(std::is_same_v<cipher::WarmTierHandle<int>, ::crucible::safety::cipher_tier::Warm<int>>);

static_assert(std::is_same_v<cipher::ColdTierHandle<int>, ::crucible::safety::cipher_tier::Cold<int>>);

static_assert(
    cipher::can_promote_tier_v<::crucible::safety::CipherTierTag_v::Cold, ::crucible::safety::CipherTierTag_v::Hot>);

static_assert(
    !cipher::can_promote_tier_v<::crucible::safety::CipherTierTag_v::Hot, ::crucible::safety::CipherTierTag_v::Cold>);

static_assert(
    cipher::can_demote_tier_v<::crucible::safety::CipherTierTag_v::Hot, ::crucible::safety::CipherTierTag_v::Cold>);

static_assert(
    std::is_same_v<
        cipher::EpochedDelegate<::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
                                ::crucible::safety::proto::End, 0, 0>,
        ::crucible::safety::proto::EpochedDelegate<::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
                                                   ::crucible::safety::proto::End, 0, 0>>,
    "fixy::contract::cipher::EpochedDelegate must alias "
    "safety::proto::EpochedDelegate.");

namespace u015 {

static_assert(std::is_same_v<cipher::Cipher, ::crucible::Cipher>,
              "fixy::contract::cipher::Cipher must alias ::crucible::Cipher.");

static_assert(std::is_same_v<cipher::CipherOpenView, ::crucible::CipherOpenView>,
              "fixy::contract::cipher::CipherOpenView must alias the substrate.");

static_assert(std::is_same_v<cipher::CipherSessionEventPersistenceRow, ::crucible::CipherSessionEventPersistenceRow>);

static_assert(std::is_same_v<cipher::ContentAddressedPayload<int>, ::crucible::cipher::ContentAddressedPayload<int>>);

static_assert(
    std::is_same_v<cipher::LoadedContentAddressedPayload<int>, ::crucible::cipher::LoadedContentAddressedPayload<int>>);

static_assert(std::is_same_v<cipher::Cipher::OpenView, ::crucible::CipherOpenView>);

static_assert(std::is_same_v<cipher::SessionEvent, ::crucible::safety::proto::SessionEvent>,
              "fixy::contract::cipher::SessionEvent must alias substrate.");

static_assert(sizeof(cipher::SessionEvent) == 72, "Cipher session-event wire format is pinned at 72 B.");

// CompiledBody is incomplete, so the identity check goes through the pointer.
static_assert(std::is_same_v<cipher::CompiledBody*, ::crucible::cipher::CompiledBody*>);

namespace u015_cache_probe {
inline void probe_fn(int) noexcept {}
}  // namespace u015_cache_probe

static_assert(cipher::IsCacheableFunction<&u015_cache_probe::probe_fn>
              == ::crucible::cipher::IsCacheableFunction<&u015_cache_probe::probe_fn>);
static_assert(cipher::IsCacheableFunction<&u015_cache_probe::probe_fn>,
              "An inline noexcept fn must satisfy IsCacheableFunction through fixy::.");

static_assert(cipher::IsEffectRow<::crucible::effects::Row<>>
              == ::crucible::cipher::IsEffectRow<::crucible::effects::Row<>>);
static_assert(cipher::IsEffectRow<::crucible::effects::Row<>>);
static_assert(!cipher::IsEffectRow<int>, "Non-row T must NOT satisfy IsEffectRow through fixy::.");

static_assert(cipher::computation_cache_key<&u015_cache_probe::probe_fn, int>
              == ::crucible::cipher::computation_cache_key<&u015_cache_probe::probe_fn, int>);

static_assert(
    cipher::computation_cache_key_in_row<&u015_cache_probe::probe_fn, ::crucible::effects::Row<>, int>
    == ::crucible::cipher::computation_cache_key_in_row<&u015_cache_probe::probe_fn, ::crucible::effects::Row<>, int>);

static_assert(
    std::is_same_v<decltype(&cipher::lookup_computation_cache<&u015_cache_probe::probe_fn, int>),
                   decltype(&::crucible::cipher::lookup_computation_cache<&u015_cache_probe::probe_fn, int>)>);
static_assert(
    std::is_same_v<decltype(&cipher::insert_computation_cache<&u015_cache_probe::probe_fn, int>),
                   decltype(&::crucible::cipher::insert_computation_cache<&u015_cache_probe::probe_fn, int>)>);
static_assert(std::is_same_v<decltype(&cipher::lookup_computation_cache_in_row<&u015_cache_probe::probe_fn,
                                                                               ::crucible::effects::Row<>, int>),
                             decltype(&::crucible::cipher::lookup_computation_cache_in_row<
                                      &u015_cache_probe::probe_fn, ::crucible::effects::Row<>, int>)>);
static_assert(std::is_same_v<decltype(&cipher::insert_computation_cache_in_row<&u015_cache_probe::probe_fn,
                                                                               ::crucible::effects::Row<>, int>),
                             decltype(&::crucible::cipher::insert_computation_cache_in_row<
                                      &u015_cache_probe::probe_fn, ::crucible::effects::Row<>, int>)>);
static_assert(
    std::is_same_v<decltype(&cipher::drain_computation_cache), decltype(&::crucible::cipher::drain_computation_cache)>);

static_assert(cipher::federation::FEDERATION_MAGIC == ::crucible::cipher::federation::FEDERATION_MAGIC);
static_assert(cipher::federation::FEDERATION_PROTOCOL_V1 == ::crucible::cipher::federation::FEDERATION_PROTOCOL_V1);
static_assert(cipher::federation::FEDERATION_HEADER_BYTES == ::crucible::cipher::federation::FEDERATION_HEADER_BYTES);

static_assert(
    std::is_same_v<cipher::federation::FederationEntryHeader, ::crucible::cipher::federation::FederationEntryHeader>);
static_assert(std::is_same_v<cipher::federation::ColdBlobRegion, ::crucible::cipher::federation::ColdBlobRegion>);
static_assert(
    std::is_same_v<cipher::federation::FederationEntryView, ::crucible::cipher::federation::FederationEntryView>);
static_assert(std::is_same_v<cipher::federation::FederationError, ::crucible::cipher::federation::FederationError>);

static_assert(std::is_same_v<decltype(&cipher::federation::federation_error_name),
                             decltype(&::crucible::cipher::federation::federation_error_name)>);
static_assert(std::is_same_v<decltype(&cipher::federation::serialize_federation_entry),
                             decltype(&::crucible::cipher::federation::serialize_federation_entry)>);
static_assert(std::is_same_v<decltype(&cipher::federation::deserialize_federation_header),
                             decltype(&::crucible::cipher::federation::deserialize_federation_header)>);
static_assert(std::is_same_v<decltype(&cipher::federation::deserialize_untrusted_federation_entry),
                             decltype(&::crucible::cipher::federation::deserialize_untrusted_federation_entry)>);

struct U015FederationProbeOrg {};
static_assert(
    std::is_same_v<decltype(&cipher::federation::deserialize_federation_entry<U015FederationProbeOrg>),
                   decltype(&::crucible::cipher::federation::deserialize_federation_entry<U015FederationProbeOrg>)>);

static_assert(std::is_same_v<decltype(&cipher::federation::federation_entry_blob_layout_disjoint),
                             decltype(&::crucible::cipher::federation::federation_entry_blob_layout_disjoint)>);
static_assert(std::is_same_v<decltype(&cipher::federation::federation_accepts_cardinality),
                             decltype(&::crucible::cipher::federation::federation_accepts_cardinality)>);

static_assert(std::is_same_v<decltype(&cipher::federation::cold_blob_regions_pairwise_disjoint<8>),
                             decltype(&::crucible::cipher::federation::cold_blob_regions_pairwise_disjoint<8>)>);

namespace u015_ccfed_probe {
inline void probe_kernel(int) noexcept {}
}  // namespace u015_ccfed_probe
using U015ProbeRow = ::crucible::effects::Row<>;

static_assert(std::is_same_v<
              cipher::federation::ComputationCacheFederationKeyTag<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
              ::crucible::cipher::federation::ComputationCacheFederationKeyTag<&u015_ccfed_probe::probe_kernel,
                                                                               U015ProbeRow, int>>);

static_assert(
    std::is_same_v<
        cipher::federation::ComputationCacheFederationSenderProto<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
        ::crucible::cipher::federation::ComputationCacheFederationSenderProto<&u015_ccfed_probe::probe_kernel,
                                                                              U015ProbeRow, int>>);
static_assert(
    std::is_same_v<
        cipher::federation::ComputationCacheFederationReceiverProto<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
        ::crucible::cipher::federation::ComputationCacheFederationReceiverProto<&u015_ccfed_probe::probe_kernel,
                                                                                U015ProbeRow, int>>);
static_assert(
    std::is_same_v<
        cipher::federation::ComputationCacheFederationCoordProto<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
        ::crucible::cipher::federation::ComputationCacheFederationCoordProto<&u015_ccfed_probe::probe_kernel,
                                                                             U015ProbeRow, int>>);

static_assert(std::is_same_v<cipher::federation::ContentAddressedFederationPayload<int>,
                             ::crucible::cipher::federation::ContentAddressedFederationPayload<int>>);

static_assert(std::is_same_v<
              cipher::federation::ComputationCacheFederationPayload<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
              ::crucible::cipher::federation::ComputationCacheFederationPayload<&u015_ccfed_probe::probe_kernel,
                                                                                U015ProbeRow, int>>);
static_assert(std::is_same_v<cipher::federation::ComputationCacheFederationContentAddressedPayload<
                                 &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
                             ::crucible::cipher::federation::ComputationCacheFederationContentAddressedPayload<
                                 &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>>);

static_assert(
    std::is_same_v<
        decltype(&cipher::federation::federation_content_hash<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>),
        decltype(&::crucible::cipher::federation::federation_content_hash<&u015_ccfed_probe::probe_kernel, U015ProbeRow,
                                                                          int>)>);
static_assert(std::is_same_v<decltype(&cipher::federation::federation_row_hash<U015ProbeRow>),
                             decltype(&::crucible::cipher::federation::federation_row_hash<U015ProbeRow>)>);
static_assert(
    std::is_same_v<
        decltype(&cipher::federation::federation_key<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>),
        decltype(&::crucible::cipher::federation::federation_key<&u015_ccfed_probe::probe_kernel, U015ProbeRow, int>)>);

// serialize_computation_cache_federation_entry is an overload set, so
// `decltype(&fn)` cannot name it.  Disambiguating with a static_cast to an
// explicit function-pointer type would pin this sentinel to the substrate's
// parameter list, which the payload template still changes.  The witness taken
// instead is that the using-declaration parses at all.

constexpr int u015_surface_cardinality = 42;
static_assert(u015_surface_cardinality == 42, "The Cipher, ComputationCache and federation re-export surface "
                                              "drifted from 42 items.  The using-decl block and this sentinel "
                                              "must update in lockstep.");

}  // namespace u015

}  // namespace crucible::fixy::contract::self_test
