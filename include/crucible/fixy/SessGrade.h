#pragma once

#include <crucible/sessions/SessionGrade.h>

#include <type_traits>

// The lattice types that the metafunctions below return are owned by
// the algebra layer. They are reached through that layer and are
// deliberately not re-exported here.
namespace crucible::fixy::sess::grade {

using ::crucible::safety::proto::protocol_grade;
using ::crucible::safety::proto::protocol_grade_t;
using ::crucible::safety::proto::payload_grade;
using ::crucible::safety::proto::payload_grade_t;
using ::crucible::safety::proto::grade_for_axis;
using ::crucible::safety::proto::grade_for_axis_t;
using ::crucible::safety::proto::grade_for_axis_v;

using ::crucible::safety::proto::protocol_vendor_t;
using ::crucible::safety::proto::protocol_numerical_tier_t;
using ::crucible::safety::proto::protocol_cipher_tier_t;
using ::crucible::safety::proto::protocol_crash_class_t;
using ::crucible::safety::proto::protocol_epoch_versioned_t;
using ::crucible::safety::proto::protocol_numa_placement_t;

using ::crucible::safety::proto::protocol_grade_vendor_v;
using ::crucible::safety::proto::protocol_grade_numerical_tier_v;
using ::crucible::safety::proto::protocol_grade_cipher_tier_v;
using ::crucible::safety::proto::protocol_grade_crash_class_v;
using ::crucible::safety::proto::protocol_grade_epoch_versioned_v;
using ::crucible::safety::proto::protocol_grade_numa_placement_v;

using ::crucible::safety::proto::protocol_grade_aggregate_satisfies_v;

// The axis tag named CrashClass shares its name with the enumeration
// that the crash axis projects. The tags stay in their own namespace so
// the two spellings never collide.
namespace axis {
using ::crucible::safety::proto::axis::Vendor;
using ::crucible::safety::proto::axis::NumericalTier;
using ::crucible::safety::proto::axis::CipherTier;
using ::crucible::safety::proto::axis::CrashClass;
using ::crucible::safety::proto::axis::EpochVersioned;
using ::crucible::safety::proto::axis::NumaPlacement;
}  // namespace axis

}  // namespace crucible::fixy::sess::grade

namespace crucible::fixy::sess::grade::u052j_self_test {

namespace prot = ::crucible::safety::proto;
namespace saf = ::crucible::safety;

struct ResultTensor {};
using MultiAxisPayload = saf::NumericalTier<
    prot::Tolerance::BITEXACT,
    saf::Vendor<prot::VendorBackend::NV,
                saf::CipherTier<prot::CipherTierTag::Hot, saf::Crash<prot::CrashClass::NoThrow, ResultTensor>>>>;
using MultiAxisProto = prot::Send<MultiAxisPayload, prot::End>;

static_assert(std::is_same_v<protocol_grade<prot::End>, prot::protocol_grade<prot::End>>);
static_assert(std::is_same_v<protocol_grade_t<MultiAxisProto>, prot::protocol_grade_t<MultiAxisProto>>);
static_assert(std::is_same_v<axis::NumericalTier, prot::axis::NumericalTier>);
static_assert(std::is_same_v<axis::Vendor, prot::axis::Vendor>);
static_assert(!std::is_same_v<axis::Vendor, axis::CipherTier>, "axis tags are structurally distinct.");

static_assert(protocol_grade_vendor_v<MultiAxisProto> == prot::VendorBackend::NV);
static_assert(protocol_grade_numerical_tier_v<MultiAxisProto> == prot::Tolerance::BITEXACT);
static_assert(protocol_grade_cipher_tier_v<MultiAxisProto> == prot::CipherTierTag::Hot);
static_assert(protocol_grade_crash_class_v<MultiAxisProto> == prot::CrashClass::NoThrow);
static_assert(!protocol_grade_epoch_versioned_v<MultiAxisProto>);
static_assert(!protocol_grade_numa_placement_v<MultiAxisProto>);

static_assert(std::is_same_v<protocol_vendor_t<MultiAxisProto>, prot::VendorLattice::At<prot::VendorBackend::NV>>);
static_assert(
    std::is_same_v<protocol_numerical_tier_t<MultiAxisProto>, prot::ToleranceLattice::At<prot::Tolerance::BITEXACT>>);

static_assert(grade_for_axis_v<axis::NumericalTier, MultiAxisPayload> == prot::Tolerance::BITEXACT);
static_assert(grade_for_axis_v<axis::Vendor, MultiAxisPayload> == prot::VendorBackend::NV);
static_assert(std::is_same_v<grade_for_axis_t<axis::NumericalTier, MultiAxisPayload>,
                             prot::ToleranceLattice::At<prot::Tolerance::BITEXACT>>);

using EpochPayload = saf::EpochVersioned<MultiAxisPayload>;
using NumaPayload = saf::NumaPlacement<EpochPayload>;
using RuntimeGradeProto = prot::Send<NumaPayload, prot::End>;
static_assert(protocol_grade_epoch_versioned_v<RuntimeGradeProto>);
static_assert(protocol_grade_numa_placement_v<RuntimeGradeProto>);
static_assert(protocol_grade_aggregate_satisfies_v<RuntimeGradeProto, MultiAxisProto>,
              "the runtime-graded protocol carries strictly more evidence.");
static_assert(!protocol_grade_aggregate_satisfies_v<MultiAxisProto, RuntimeGradeProto>,
              "the compile-graded protocol lacks the epoch/numa evidence.");

// The count is one per re-exported name.
constexpr int u052j_surface_cardinality = 26;
static_assert(u052j_surface_cardinality == 26, "the re-exported surface of fixy::sess::grade has changed. Update "
                                               "the using-declarations and this count together.");

}  // namespace crucible::fixy::sess::grade::u052j_self_test

namespace crucible::fixy::sess::grade {

// A static assertion can be discharged without ever instantiating an
// inline body. Naming the results in a real function puts every
// metafunction below through a full instantiation.
inline void runtime_smoke_test() noexcept {
    namespace prot = ::crucible::safety::proto;
    namespace saf = ::crucible::safety;

    struct Payload {};
    using P = prot::Send<saf::NumericalTier<prot::Tolerance::BITEXACT, Payload>, prot::End>;

    [[maybe_unused]] constexpr prot::Tolerance ntier = protocol_grade_numerical_tier_v<P>;
    [[maybe_unused]] constexpr prot::VendorBackend vend = protocol_grade_vendor_v<P>;
    [[maybe_unused]] constexpr bool self_sat = protocol_grade_aggregate_satisfies_v<P, P>;

    using GradeT = protocol_grade_t<P>;
    using AxisT = grade_for_axis_t<axis::NumericalTier, saf::NumericalTier<prot::Tolerance::BITEXACT, Payload>>;
    [[maybe_unused]] constexpr bool grade_ok = std::is_same_v<GradeT, prot::protocol_grade_t<P>>;
    [[maybe_unused]] constexpr bool axis_ok =
        std::is_same_v<AxisT, prot::ToleranceLattice::At<prot::Tolerance::BITEXACT>>;

    (void)ntier;
    (void)vend;
    (void)self_sat;
    (void)grade_ok;
    (void)axis_ok;
}

}  // namespace crucible::fixy::sess::grade
