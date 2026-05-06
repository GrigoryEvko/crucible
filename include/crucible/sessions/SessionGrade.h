#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — protocol grade projection
//
// GAPS-069.  Projects a session protocol into a compile-time product
// grade over the payload axes that already have shipped lattices:
//
//   ProductLattice<
//       VendorLattice::At<V>,
//       ToleranceLattice::At<N>,
//       CipherTierLattice::At<C>,
//       CrashLattice::At<K>,
//       PresenceLattice::At<EpochVersioned>,
//       PresenceLattice::At<NumaPlacement>>
//
// This is deliberately a compile-time introspection layer.  GAPS-070
// consumes protocol_grade_t<P> as the orthogonal subtype filter.
// Every operation below is type-level or constexpr.  No runtime
// verification path is introduced in the headers.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/algebra/lattices/CipherTierLattice.h>
#include <crucible/algebra/lattices/CrashLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>
#include <crucible/algebra/lattices/VendorLattice.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/Vendor.h>
#include <crucible/sessions/Session.h>

#include <string_view>
#include <type_traits>

namespace crucible::safety::proto {

using ::crucible::algebra::lattices::CipherTierLattice;
using ::crucible::algebra::lattices::CipherTierTag;
using ::crucible::algebra::lattices::CrashClass;
using ::crucible::algebra::lattices::CrashLattice;
using ::crucible::algebra::lattices::ProductLattice;
using ::crucible::algebra::lattices::Tolerance;
using ::crucible::algebra::lattices::ToleranceLattice;
using ::crucible::algebra::lattices::VendorBackend;
using ::crucible::algebra::lattices::VendorLattice;

template <typename T> struct ContentAddressed;
template <typename T, typename Tag> struct Transferable;
template <typename T, typename Tag> struct Borrowed;
template <typename T, typename Tag> struct Returned;
template <typename InnerProto, typename InnerPS> struct DelegatedSession;
template <typename T, typename K> struct Delegate;
template <typename T, typename K> struct Accept;
template <typename Base, typename Rollback> struct CheckpointedSession;
template <CrashClass C> struct Stop_g;

namespace axis {
struct Vendor {};
struct NumericalTier {};
struct CipherTier {};
struct CrashClass {};
struct EpochVersioned {};
struct NumaPlacement {};
}  // namespace axis

namespace detail::session_grade {

struct PresenceLattice {
    using element_type = bool;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return false;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return true;
    }
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept {
        return !a || b;
    }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept {
        return a || b;
    }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept {
        return a && b;
    }
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "PresenceLattice";
    }

    template <bool Present>
    struct At {
        struct element_type {
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr bool present = Present;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            if constexpr (Present) {
                return "PresenceLattice::At<present>";
            } else {
                return "PresenceLattice::At<absent>";
            }
        }
    };
};

template <VendorBackend V,
          Tolerance N,
          CipherTierTag C,
          CrashClass K,
          bool E,
          bool M>
struct make {
    using type = ProductLattice<
        VendorLattice::At<V>,
        ToleranceLattice::At<N>,
        CipherTierLattice::At<C>,
        CrashLattice::At<K>,
        PresenceLattice::At<E>,
        PresenceLattice::At<M>>;
};

template <VendorBackend V,
          Tolerance N,
          CipherTierTag C,
          CrashClass K,
          bool E,
          bool M>
using make_t = typename make<V, N, C, K, E, M>::type;

using bottom_t = make_t<
    VendorBackend::None,
    Tolerance::RELAXED,
    CipherTierTag::Cold,
    CrashClass::Abort,
    false,
    false>;

template <typename Grade>
struct values;

template <VendorBackend V,
          Tolerance N,
          CipherTierTag C,
          CrashClass K,
          bool E,
          bool M>
struct values<ProductLattice<
    VendorLattice::At<V>,
    ToleranceLattice::At<N>,
    CipherTierLattice::At<C>,
    CrashLattice::At<K>,
    PresenceLattice::At<E>,
    PresenceLattice::At<M>>> {
    static constexpr VendorBackend vendor = V;
    static constexpr Tolerance numerical_tier = N;
    static constexpr CipherTierTag cipher_tier = C;
    static constexpr CrashClass crash_class = K;
    static constexpr bool epoch_versioned = E;
    static constexpr bool numa_placement = M;
};

template <typename A, typename B>
struct join {
    using av = values<A>;
    using bv = values<B>;

    using type = make_t<
        VendorLattice::join(av::vendor, bv::vendor),
        ToleranceLattice::join(av::numerical_tier, bv::numerical_tier),
        CipherTierLattice::join(av::cipher_tier, bv::cipher_tier),
        CrashLattice::join(av::crash_class, bv::crash_class),
        PresenceLattice::join(av::epoch_versioned, bv::epoch_versioned),
        PresenceLattice::join(av::numa_placement, bv::numa_placement)>;
};

template <typename A, typename B>
using join_t = typename join<A, B>::type;

template <typename... Grades>
struct join_many {
    using type = bottom_t;
};

template <typename G>
struct join_many<G> {
    using type = G;
};

template <typename G0, typename G1, typename... Rest>
struct join_many<G0, G1, Rest...> {
    using type = join_t<G0, typename join_many<G1, Rest...>::type>;
};

template <typename... Grades>
using join_many_t = typename join_many<Grades...>::type;

template <typename Provided, typename Required>
struct satisfies {
    using pv = values<Provided>;
    using rv = values<Required>;

    static constexpr bool value =
        VendorLattice::leq(rv::vendor, pv::vendor) &&
        ToleranceLattice::leq(rv::numerical_tier, pv::numerical_tier) &&
        CipherTierLattice::leq(rv::cipher_tier, pv::cipher_tier) &&
        CrashLattice::leq(rv::crash_class, pv::crash_class) &&
        PresenceLattice::leq(rv::epoch_versioned, pv::epoch_versioned) &&
        PresenceLattice::leq(rv::numa_placement, pv::numa_placement);
};

template <typename Provided, typename Required>
inline constexpr bool satisfies_v = satisfies<Provided, Required>::value;

}  // namespace detail::session_grade

template <typename P>
struct protocol_grade;

template <typename P>
using protocol_grade_t = typename protocol_grade<P>::type;

template <typename Payload>
struct payload_grade {
    using type = detail::session_grade::bottom_t;
};

template <typename Payload>
using payload_grade_t =
    typename payload_grade<std::remove_cvref_t<Payload>>::type;

template <VendorBackend V, typename T>
struct payload_grade<::crucible::safety::Vendor<V, T>> {
    using type = detail::session_grade::join_t<
        detail::session_grade::make_t<
            V,
            Tolerance::RELAXED,
            CipherTierTag::Cold,
            CrashClass::Abort,
            false,
            false>,
        payload_grade_t<T>>;
};

template <Tolerance N, typename T>
struct payload_grade<::crucible::safety::NumericalTier<N, T>> {
    using type = detail::session_grade::join_t<
        detail::session_grade::make_t<
            VendorBackend::None,
            N,
            CipherTierTag::Cold,
            CrashClass::Abort,
            false,
            false>,
        payload_grade_t<T>>;
};

template <CipherTierTag C, typename T>
struct payload_grade<::crucible::safety::CipherTier<C, T>> {
    using type = detail::session_grade::join_t<
        detail::session_grade::make_t<
            VendorBackend::None,
            Tolerance::RELAXED,
            C,
            CrashClass::Abort,
            false,
            false>,
        payload_grade_t<T>>;
};

template <CrashClass K, typename T>
struct payload_grade<::crucible::safety::Crash<K, T>> {
    using type = detail::session_grade::join_t<
        detail::session_grade::make_t<
            VendorBackend::None,
            Tolerance::RELAXED,
            CipherTierTag::Cold,
            K,
            false,
            false>,
        payload_grade_t<T>>;
};

// EpochVersioned<T> and NumaPlacement<T> carry runtime coordinates,
// not type-pinned epoch/node values.  The protocol grade records the
// compile-time evidence that the wrapper is present, so subtype checks
// can reject unauthorised attempts to regain that evidence from a bare
// payload.  Per-instance leq remains at the wrapper admission gate.
template <typename T>
struct payload_grade<::crucible::safety::EpochVersioned<T>> {
    using type = detail::session_grade::join_t<
        detail::session_grade::make_t<
            VendorBackend::None,
            Tolerance::RELAXED,
            CipherTierTag::Cold,
            CrashClass::Abort,
            true,
            false>,
        payload_grade_t<T>>;
};

template <typename T>
struct payload_grade<::crucible::safety::NumaPlacement<T>> {
    using type = detail::session_grade::join_t<
        detail::session_grade::make_t<
            VendorBackend::None,
            Tolerance::RELAXED,
            CipherTierTag::Cold,
            CrashClass::Abort,
            false,
            true>,
        payload_grade_t<T>>;
};

template <typename T>
struct payload_grade<ContentAddressed<T>> : payload_grade<T> {};

template <typename T, typename Tag>
struct payload_grade<Transferable<T, Tag>> : payload_grade<T> {};

template <typename T, typename Tag>
struct payload_grade<Borrowed<T, Tag>> : payload_grade<T> {};

template <typename T, typename Tag>
struct payload_grade<Returned<T, Tag>> : payload_grade<T> {};

template <typename InnerProto, typename InnerPS>
struct payload_grade<DelegatedSession<InnerProto, InnerPS>> {
    using type = protocol_grade_t<InnerProto>;
};

template <typename Axis, typename T>
struct grade_for_axis;

template <typename T>
struct grade_for_axis<axis::Vendor, T> {
    static constexpr VendorBackend value =
        detail::session_grade::values<payload_grade_t<T>>::vendor;
    using type = VendorLattice::At<value>;
};

template <typename T>
struct grade_for_axis<axis::NumericalTier, T> {
    static constexpr Tolerance value =
        detail::session_grade::values<payload_grade_t<T>>::numerical_tier;
    using type = ToleranceLattice::At<value>;
};

template <typename T>
struct grade_for_axis<axis::CipherTier, T> {
    static constexpr CipherTierTag value =
        detail::session_grade::values<payload_grade_t<T>>::cipher_tier;
    using type = CipherTierLattice::At<value>;
};

template <typename T>
struct grade_for_axis<axis::CrashClass, T> {
    static constexpr CrashClass value =
        detail::session_grade::values<payload_grade_t<T>>::crash_class;
    using type = CrashLattice::At<value>;
};

template <typename T>
struct grade_for_axis<axis::EpochVersioned, T> {
    static constexpr bool value =
        detail::session_grade::values<payload_grade_t<T>>::epoch_versioned;
    using type = detail::session_grade::PresenceLattice::At<value>;
};

template <typename T>
struct grade_for_axis<axis::NumaPlacement, T> {
    static constexpr bool value =
        detail::session_grade::values<payload_grade_t<T>>::numa_placement;
    using type = detail::session_grade::PresenceLattice::At<value>;
};

template <typename Axis, typename T>
using grade_for_axis_t = typename grade_for_axis<Axis, T>::type;

template <typename Axis, typename T>
inline constexpr auto grade_for_axis_v =
    grade_for_axis<Axis, T>::value;

template <typename P>
struct protocol_grade {
    static_assert(sizeof(P) == 0,
        "protocol_grade<P>: unsupported session combinator.  Add a "
        "SessionGrade.h specialization so the protocol grade cannot "
        "silently collapse to bottom.");
};

template <>
struct protocol_grade<End> {
    using type = detail::session_grade::bottom_t;
};

template <>
struct protocol_grade<Continue> {
    using type = detail::session_grade::bottom_t;
};

template <typename T, typename K>
struct protocol_grade<Send<T, K>> {
    using type = detail::session_grade::join_t<
        payload_grade_t<T>,
        protocol_grade_t<K>>;
};

template <typename T, typename K>
struct protocol_grade<Recv<T, K>> {
    using type = detail::session_grade::join_t<
        payload_grade_t<T>,
        protocol_grade_t<K>>;
};

template <typename Body>
struct protocol_grade<Loop<Body>> : protocol_grade<Body> {};

template <typename... Branches>
struct protocol_grade<Select<Branches...>> {
    using type = detail::session_grade::join_many_t<
        protocol_grade_t<Branches>...>;
};

template <typename... Branches>
struct protocol_grade<Offer<Branches...>> {
    using type = detail::session_grade::join_many_t<
        protocol_grade_t<Branches>...>;
};

template <typename Role, typename... Branches>
struct protocol_grade<Offer<Sender<Role>, Branches...>> {
    using type = detail::session_grade::join_many_t<
        protocol_grade_t<Branches>...>;
};

template <VendorBackend V, typename P>
struct protocol_grade<VendorPinned<V, P>> {
    using type = detail::session_grade::join_t<
        detail::session_grade::make_t<
            V,
            Tolerance::RELAXED,
            CipherTierTag::Cold,
            CrashClass::Abort,
            false,
            false>,
        protocol_grade_t<P>>;
};

template <CrashClass C>
struct protocol_grade<Stop_g<C>> {
    using type = detail::session_grade::make_t<
        VendorBackend::None,
        Tolerance::RELAXED,
        CipherTierTag::Cold,
        C,
        false,
        false>;
};

template <typename T, typename K>
struct protocol_grade<Delegate<T, K>> {
    using type = detail::session_grade::join_t<
        protocol_grade_t<T>,
        protocol_grade_t<K>>;
};

template <typename T, typename K>
struct protocol_grade<Accept<T, K>>
    : protocol_grade<Delegate<T, K>> {};

template <typename Base, typename Rollback>
struct protocol_grade<CheckpointedSession<Base, Rollback>> {
    using type = detail::session_grade::join_t<
        protocol_grade_t<Base>,
        protocol_grade_t<Rollback>>;
};

template <typename P>
using protocol_vendor_t =
    VendorLattice::At<
        detail::session_grade::values<protocol_grade_t<P>>::vendor>;

template <typename P>
using protocol_numerical_tier_t =
    ToleranceLattice::At<
        detail::session_grade::values<
            protocol_grade_t<P>>::numerical_tier>;

template <typename P>
using protocol_cipher_tier_t =
    CipherTierLattice::At<
        detail::session_grade::values<protocol_grade_t<P>>::cipher_tier>;

template <typename P>
using protocol_crash_class_t =
    CrashLattice::At<
        detail::session_grade::values<protocol_grade_t<P>>::crash_class>;

template <typename P>
using protocol_epoch_versioned_t =
    detail::session_grade::PresenceLattice::At<
        detail::session_grade::values<
            protocol_grade_t<P>>::epoch_versioned>;

template <typename P>
using protocol_numa_placement_t =
    detail::session_grade::PresenceLattice::At<
        detail::session_grade::values<
            protocol_grade_t<P>>::numa_placement>;

template <typename P>
inline constexpr VendorBackend protocol_grade_vendor_v =
    detail::session_grade::values<protocol_grade_t<P>>::vendor;

template <typename P>
inline constexpr Tolerance protocol_grade_numerical_tier_v =
    detail::session_grade::values<protocol_grade_t<P>>::numerical_tier;

template <typename P>
inline constexpr CipherTierTag protocol_grade_cipher_tier_v =
    detail::session_grade::values<protocol_grade_t<P>>::cipher_tier;

template <typename P>
inline constexpr CrashClass protocol_grade_crash_class_v =
    detail::session_grade::values<protocol_grade_t<P>>::crash_class;

template <typename P>
inline constexpr bool protocol_grade_epoch_versioned_v =
    detail::session_grade::values<protocol_grade_t<P>>::epoch_versioned;

template <typename P>
inline constexpr bool protocol_grade_numa_placement_v =
    detail::session_grade::values<protocol_grade_t<P>>::numa_placement;

template <typename Provided, typename Required>
inline constexpr bool protocol_grade_aggregate_satisfies_v =
    detail::session_grade::satisfies_v<
        protocol_grade_t<Provided>,
        protocol_grade_t<Required>>;

namespace detail::session_grade_self_test {

struct ResultTensor {};
struct Peer {};
struct WorkPerm {};
struct RoleA {};

using MultiAxisPayload =
    ::crucible::safety::NumericalTier<
        Tolerance::BITEXACT,
        ::crucible::safety::Vendor<
            VendorBackend::NV,
            ::crucible::safety::CipherTier<
                CipherTierTag::Hot,
                ::crucible::safety::Crash<
                    CrashClass::NoThrow,
                    ResultTensor>>>>;

using MultiAxisProto = Send<MultiAxisPayload, End>;
using MultiAxisExpected = ProductLattice<
    VendorLattice::At<VendorBackend::NV>,
    ToleranceLattice::At<Tolerance::BITEXACT>,
    CipherTierLattice::At<CipherTierTag::Hot>,
    CrashLattice::At<CrashClass::NoThrow>,
    detail::session_grade::PresenceLattice::At<false>,
    detail::session_grade::PresenceLattice::At<false>>;

static_assert(std::is_same_v<
    protocol_grade_t<MultiAxisProto>,
    MultiAxisExpected>);
static_assert(std::is_same_v<
    protocol_vendor_t<MultiAxisProto>,
    VendorLattice::At<VendorBackend::NV>>);
static_assert(std::is_same_v<
    protocol_numerical_tier_t<MultiAxisProto>,
    ToleranceLattice::At<Tolerance::BITEXACT>>);
static_assert(std::is_same_v<
    protocol_cipher_tier_t<MultiAxisProto>,
    CipherTierLattice::At<CipherTierTag::Hot>>);
static_assert(std::is_same_v<
    protocol_crash_class_t<MultiAxisProto>,
    CrashLattice::At<CrashClass::NoThrow>>);
static_assert(std::is_same_v<
    protocol_epoch_versioned_t<MultiAxisProto>,
    detail::session_grade::PresenceLattice::At<false>>);
static_assert(std::is_same_v<
    protocol_numa_placement_t<MultiAxisProto>,
    detail::session_grade::PresenceLattice::At<false>>);

using BranchJoinProto = Select<
    Send<::crucible::safety::NumericalTier<
             Tolerance::ULP_FP16, int>, End>,
    Send<::crucible::safety::NumericalTier<
             Tolerance::BITEXACT, int>, End>>;
static_assert(protocol_grade_numerical_tier_v<BranchJoinProto>
              == Tolerance::BITEXACT);

using PermissionMarkerProto = Send<
    Transferable<ContentAddressed<MultiAxisPayload>, WorkPerm>,
    End>;
static_assert(std::is_same_v<
    protocol_grade_t<PermissionMarkerProto>,
    MultiAxisExpected>);

using PinnedProto = VendorPinned<
    VendorBackend::AMD,
    Recv<int, End>>;
static_assert(protocol_grade_vendor_v<PinnedProto>
              == VendorBackend::AMD);

using AnnotatedOffer = Offer<
    Sender<RoleA>,
    Recv<MultiAxisPayload, End>,
    Send<int, End>>;
static_assert(protocol_grade_vendor_v<AnnotatedOffer>
              == VendorBackend::NV);

using EpochPayload =
    ::crucible::safety::EpochVersioned<MultiAxisPayload>;
using NumaPayload =
    ::crucible::safety::NumaPlacement<EpochPayload>;
using RuntimeGradeProto = Send<NumaPayload, End>;
static_assert(protocol_grade_epoch_versioned_v<RuntimeGradeProto>);
static_assert(protocol_grade_numa_placement_v<RuntimeGradeProto>);
static_assert(protocol_grade_vendor_v<RuntimeGradeProto>
              == VendorBackend::NV);
static_assert(protocol_grade_aggregate_satisfies_v<RuntimeGradeProto,
                                                   MultiAxisProto>);
static_assert(!protocol_grade_aggregate_satisfies_v<MultiAxisProto,
                                                    RuntimeGradeProto>);

}  // namespace detail::session_grade_self_test

}  // namespace crucible::safety::proto
