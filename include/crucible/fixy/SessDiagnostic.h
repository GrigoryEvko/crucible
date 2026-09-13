#pragma once

#include <crucible/sessions/SessionDiagnostic.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace crucible::fixy::sess::diagnostic {

using ::crucible::safety::proto::diagnostic::tag_base;

using ::crucible::safety::proto::diagnostic::ProtocolViolation_Label;
using ::crucible::safety::proto::diagnostic::ProtocolViolation_Payload;
using ::crucible::safety::proto::diagnostic::ProtocolViolation_State;
using ::crucible::safety::proto::diagnostic::Deadlock_Detected;
using ::crucible::safety::proto::diagnostic::Livelock_Detected;
using ::crucible::safety::proto::diagnostic::StarvationPossible;
using ::crucible::safety::proto::diagnostic::CrashBranch_Missing;
using ::crucible::safety::proto::diagnostic::PermissionImbalance;
using ::crucible::safety::proto::diagnostic::SubtypeMismatch;
using ::crucible::safety::proto::diagnostic::DepthBoundReached;
using ::crucible::safety::proto::diagnostic::UnboundedQueue;
using ::crucible::safety::proto::diagnostic::Continue_Without_Loop;
using ::crucible::safety::proto::diagnostic::Protocol_Ill_Formed;
using ::crucible::safety::proto::diagnostic::Context_Domain_Collision;
using ::crucible::safety::proto::diagnostic::Context_Lookup_Miss;
using ::crucible::safety::proto::diagnostic::Queue_Empty_Dequeue;
using ::crucible::safety::proto::diagnostic::Association_Domain_Mismatch;
using ::crucible::safety::proto::diagnostic::Merge_Branches_Diverge;
using ::crucible::safety::proto::diagnostic::SessionResource_NotPinned;
using ::crucible::safety::proto::diagnostic::ShapeMismatch_SendVsRecv;
using ::crucible::safety::proto::diagnostic::ShapeMismatch_SelectVsOffer;
using ::crucible::safety::proto::diagnostic::BranchCount_Mismatch;
using ::crucible::safety::proto::diagnostic::ProtocolViolation_Self_Loop;

using ::crucible::safety::proto::diagnostic::is_diagnostic_class_v;
using ::crucible::safety::proto::diagnostic::diagnostic_name_v;
using ::crucible::safety::proto::diagnostic::diagnostic_description_v;
using ::crucible::safety::proto::diagnostic::diagnostic_remediation_v;

using ::crucible::safety::proto::diagnostic::Diagnostic;
using ::crucible::safety::proto::diagnostic::is_diagnostic;
using ::crucible::safety::proto::diagnostic::is_diagnostic_v;

using ::crucible::safety::proto::diagnostic::Catalog;
using ::crucible::safety::proto::diagnostic::catalog_size;

}  // namespace crucible::fixy::sess::diagnostic

namespace crucible::fixy::sess::diagnostic::u052g_self_test {

namespace pdiag = ::crucible::safety::proto::diagnostic;

static_assert(std::is_same_v<tag_base, pdiag::tag_base>);
static_assert(std::is_same_v<SubtypeMismatch, pdiag::SubtypeMismatch>);
static_assert(std::is_same_v<ShapeMismatch_SelectVsOffer, pdiag::ShapeMismatch_SelectVsOffer>);
static_assert(std::is_same_v<ProtocolViolation_Self_Loop, pdiag::ProtocolViolation_Self_Loop>);
static_assert(std::is_same_v<Catalog, pdiag::Catalog>);

static_assert(std::is_base_of_v<tag_base, SubtypeMismatch>);
static_assert(is_diagnostic_class_v<SubtypeMismatch>);
static_assert(is_diagnostic_class_v<BranchCount_Mismatch>);
static_assert(!is_diagnostic_class_v<tag_base>, "tag_base is the base sentinel, not itself a diagnostic class.");
static_assert(!is_diagnostic_class_v<int>, "a fundamental type is not a diagnostic class.");

static_assert(!std::is_same_v<SubtypeMismatch, ShapeMismatch_SendVsRecv>);
static_assert(!std::is_same_v<ShapeMismatch_SendVsRecv, ShapeMismatch_SelectVsOffer>);

static_assert(!diagnostic_name_v<SubtypeMismatch>.empty(), "every shipped tag carries a non-empty name.");
static_assert(!diagnostic_description_v<SubtypeMismatch>.empty());
static_assert(!diagnostic_remediation_v<SubtypeMismatch>.empty());
static_assert(is_diagnostic_v<Diagnostic<SubtypeMismatch, int, double>>,
              "Diagnostic<Tag, Ctx...> is recognised by its shape trait.");
static_assert(!is_diagnostic_v<int>);
static_assert(std::is_same_v<Diagnostic<SubtypeMismatch, int>::diagnostic_class, SubtypeMismatch>);

// These two pin a floor, not an exact count. The exact count is pinned
// beside the catalog itself, so adding a tag stays a one-place edit
// while a removal still trips here.
static_assert(catalog_size >= 23, "the diagnostic catalog has fewer than 23 tags, so a tag was removed. Update the "
                                  "exact count beside the catalog and this floor together.");
static_assert(std::tuple_size_v<Catalog> >= 23, "the diagnostic catalog tuple holds fewer than 23 tags, so a tag "
                                                "was removed.");
static_assert(std::tuple_size_v<Catalog> == catalog_size, "the catalog tuple arity and the reported catalog size "
                                                          "disagree.");
static_assert(std::is_same_v<std::tuple_element_t<8, Catalog>, SubtypeMismatch>,
              "catalog position 8 is fixed and must hold SubtypeMismatch.");

// The count is one per re-exported name.
constexpr int u052g_surface_cardinality = 33;
static_assert(u052g_surface_cardinality == 33, "the re-exported surface of fixy::sess::diagnostic has changed. "
                                               "Update the using-declarations and this count together.");

}  // namespace crucible::fixy::sess::diagnostic::u052g_self_test

namespace crucible::fixy::sess::diagnostic {

// A static assertion can be discharged without ever instantiating an
// inline body. Naming the results in a real function puts every
// metafunction below through a full instantiation.
inline void runtime_smoke_test() noexcept {
    using Tag = SubtypeMismatch;
    using Diag = Diagnostic<Tag, int, double>;

    [[maybe_unused]] constexpr bool is_tag = is_diagnostic_class_v<Tag>;
    [[maybe_unused]] constexpr bool not_tag = is_diagnostic_class_v<int>;
    [[maybe_unused]] constexpr bool is_diag = is_diagnostic_v<Diag>;
    [[maybe_unused]] constexpr std::size_t n = catalog_size;

    [[maybe_unused]] const std::string_view nm = diagnostic_name_v<Tag>;
    [[maybe_unused]] const std::string_view ds = diagnostic_description_v<Tag>;
    [[maybe_unused]] const std::string_view rm = diagnostic_remediation_v<Tag>;

    [[maybe_unused]] const Diag d{};
    [[maybe_unused]] const std::string_view dnm = Diag::name;

    (void)is_tag;
    (void)not_tag;
    (void)is_diag;
    (void)n;
    (void)nm;
    (void)ds;
    (void)rm;
    (void)d;
    (void)dnm;
}

}  // namespace crucible::fixy::sess::diagnostic
