// Sentinel TU for foundation/diag: the diag headers compile together under
// the project warning set, the accessors run with non-constant arguments,
// the three insight registration forms expand the way a consumer writes
// them, and the runtime sink is driven in both output shapes.

#include <foundation/diag/Catalog.h>
#include <foundation/diag/Insights.h>
#include <foundation/diag/JsonEmitter.h>
#include <foundation/diag/Runtime.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/ReflectionList.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace diag = ::foundation::diag;

namespace {

using ::fixy::TextView;
using ::fixy::check::Tally;

// The context of the test, which each check of the Check family takes.  Each
// check of a cell counts in a Tally, and a cell keeps going after a failure,
// so one run reports every failure it has.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// ── Catalog: compile-time cells ──────────────────────────────────────────

// The first and last tags are read out of the catalog rather than named
// here.  Naming one would make every append to the catalog redden this
// file, which tells the appender nothing about what actually broke.
using LeadingTag = std::tuple_element_t<0, diag::Catalog<>>;
using TrailingTag = std::tuple_element_t<diag::catalog_size - 1, diag::Catalog<>>;

static_assert(diag::category_of_v<LeadingTag> == diag::categories_v.front());
static_assert(diag::category_of_v<TrailingTag> == diag::categories_v.back());
static_assert(std::is_same_v<diag::tag_of_t<diag::categories_v.back()>, TrailingTag>);

// The enum is walked by reflection here.  The count of its enumerators is
// the size of the tuple.
static_assert(std::meta::enumerators_of(^^diag::Category).size() == diag::catalog_size);

static_assert(diag::category_of_v<diag::EffectRowMismatch> == diag::Category::EffectRowMismatch);
static_assert(diag::category_of_v<diag::DetSafeLeak> == diag::Category::DetSafeLeak);
static_assert(diag::category_of_v<diag::RecipeSpecMismatch> == diag::Category::RecipeSpecMismatch);

static_assert(std::is_same_v<diag::tag_of_t<diag::Category::EffectRowMismatch>, diag::EffectRowMismatch>);
static_assert(std::is_same_v<diag::tag_of_t<diag::Category::HotPathViolation>, diag::HotPathViolation>);

// The fixture lives at namespace scope because a function-local class
// may not have static data members, and a tag is all static members.
struct anonymous_local_tag : diag::tag_base {
    static constexpr diag::tag_text_literal name = "AnonymousLocalTag";
    static constexpr diag::tag_text_literal description = "test fixture";
    static constexpr diag::tag_text_literal remediation = "rebuild";
};

static_assert(diag::is_diagnostic_class_v<diag::EffectRowMismatch>);
static_assert(diag::is_diagnostic_class_v<diag::DetSafeLeak>);
static_assert(diag::is_diagnostic_class_v<anonymous_local_tag>);
static_assert(!diag::is_diagnostic_class_v<diag::tag_base>);
static_assert(!diag::is_diagnostic_class_v<int>);
static_assert(diag::diagnostic_name_v<anonymous_local_tag> == "AnonymousLocalTag");

using wrapped_t = diag::Diagnostic<diag::EffectRowMismatch, int, float>;
static_assert(diag::is_diagnostic_v<wrapped_t>);
static_assert(!diag::is_diagnostic_v<int>);
static_assert(!diag::is_diagnostic_v<diag::EffectRowMismatch>);
static_assert(std::is_same_v<typename wrapped_t::diagnostic_class, diag::EffectRowMismatch>);
static_assert(std::is_same_v<typename wrapped_t::context, std::tuple<int, float>>);
static_assert(wrapped_t::name == "EffectRowMismatch");

// Only a tag names the wrapper.  The constraint on the class template
// refuses a plain type and the root tag_base, as the factory refuses them.
template <typename Tag>
concept NamesDiagnostic = requires { typename diag::Diagnostic<Tag, float, double>; };
static_assert(NamesDiagnostic<diag::EffectRowMismatch> && NamesDiagnostic<anonymous_local_tag>);
static_assert(!NamesDiagnostic<int> && !NamesDiagnostic<diag::tag_base>);

// The factory strips qualifiers, so two call sites that differ only in
// constness produce one type.
static_assert(std::is_same_v<decltype(diag::mint_diagnostic<diag::EffectRowMismatch>(int{}, float{})),
                             diag::Diagnostic<diag::EffectRowMismatch, int, float>>);
static_assert(std::is_same_v<decltype(diag::mint_diagnostic<diag::HotPathViolation>()),
                             diag::Diagnostic<diag::HotPathViolation>>);
static_assert(std::is_same_v<decltype(diag::mint_diagnostic<diag::DetSafeLeak>(std::declval<int const&>())),
                             diag::Diagnostic<diag::DetSafeLeak, int>>);

CRUCIBLE_DIAG_ASSERT(true, EffectRowMismatch, "test_diag happy path: condition true.");
// The parentheses keep the comma inside the template argument list from
// splitting the macro's arguments.
CRUCIBLE_DIAG_ASSERT((std::is_same_v<int, int>), HotPathViolation, "Comma in condition protected by parentheses.");

constexpr std::size_t enumerate_count = []() consteval {
    std::size_t n = 0;
    diag::enumerate_categories([&n]<diag::Category /*C*/>() noexcept { ++n; });
    return n;
}();
static_assert(enumerate_count == diag::catalog_size, "enumerate_categories did not visit every Category");

// ── Catalog: runtime cells ───────────────────────────────────────────────

namespace smoke {

// A local class may not have static data members, so the fixture tag for the
// smoke test below lives at namespace scope.
struct smoke_local_tag : diag::tag_base {
    static constexpr diag::tag_text_literal name = "SmokeLocalTag";
    static constexpr diag::tag_text_literal description = "runtime smoke probe";
    static constexpr diag::tag_text_literal remediation = "this tag exists only as the smoke-test fixture";
};

}  // namespace smoke

// The checks of the header are all constant-evaluated.  This one runs
// the same accessors with non-constant arguments, which is where an
// inline-body defect in an accessor would surface.  The body names
// foundation::diag without a qualifier, so it opens that namespace with
// a using-directive.
void test_runtime_smoke() {
    using namespace ::foundation::diag;
    // The volatile bound stops the optimizer from folding the loop back into a
    // constant, which would put the switches back on the compile-time path.
    volatile std::size_t const cap = catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        Category const c = static_cast<Category>(i);
        TextView const n = name_of(c);
        TextView const d = description_of(c);
        TextView const r = remediation_of(c);

        volatile std::size_t sink = 0;
        sink ^= n.size();
        sink ^= d.size();
        sink ^= r.size();
        (void)sink;
    }

    bool const is_tag = is_diagnostic_class_v<smoke::smoke_local_tag>;
    volatile bool sink_b = is_tag;
    (void)sink_b;

    using d_t = Diagnostic<EffectRowMismatch, int, float>;
    volatile std::size_t sink_n = d_t::name.size();
    (void)sink_n;
}

// The bound is a floor, not an equality.  The catalog is append-only, so
// this guards against it shrinking; the header derives the exact count
// from the enum and pins it to the tuple itself.
void test_catalog_floor(Tally& checks) { CRUCIBLE_TEST_EXPECT(checks, diag::catalog_size >= std::size_t{33}); }

void test_accessor_runtime_coverage(Tally& checks) {
    // The volatile bound stops the loop folding away, so each accessor
    // is entered with a value the compiler cannot see through.
    volatile std::size_t const cap = diag::catalog_size;
    constexpr TextView sentinel{"<unknown Category>"};
    for (std::size_t i = 0; i < cap; ++i) {
        diag::Category const c = static_cast<diag::Category>(i);
        TextView const n = diag::name_of(c);
        TextView const d = diag::description_of(c);
        TextView const r = diag::remediation_of(c);
        CRUCIBLE_TEST_EXPECT(checks, n.size() != 0);
        CRUCIBLE_TEST_EXPECT(checks, d.size() != 0);
        CRUCIBLE_TEST_EXPECT(checks, r.size() != 0);
        CRUCIBLE_TEST_EXPECT(checks, n != sentinel);
        CRUCIBLE_TEST_EXPECT(checks, d != sentinel);
        CRUCIBLE_TEST_EXPECT(checks, r != sentinel);
    }

    // A value outside the enumeration fails the range check and comes
    // back as the sentinel.  Only a cast can produce such a value, which
    // is why the test has to make one by hand.
    diag::Category const bogus = static_cast<diag::Category>(255);
    CRUCIBLE_TEST_EXPECT(checks, diag::name_of(bogus) == sentinel);
    CRUCIBLE_TEST_EXPECT(checks, diag::description_of(bogus) == sentinel);
    CRUCIBLE_TEST_EXPECT(checks, diag::remediation_of(bogus) == sentinel);
}

void test_categories_array(Tally& checks) {
    CRUCIBLE_TEST_EXPECT(checks, diag::categories_v.size() == diag::catalog_size);
    CRUCIBLE_TEST_EXPECT(checks, diag::categories_v[0] == diag::category_of_v<LeadingTag>);
    CRUCIBLE_TEST_EXPECT(checks, diag::categories_v[diag::catalog_size - 1] == diag::category_of_v<TrailingTag>);

    // The array index and the enumerator value have to stay the same
    // number, because the accessors above index by enumerator.
    volatile std::size_t const cap = diag::catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        CRUCIBLE_TEST_EXPECT(checks, diag::categories_v[i] == static_cast<diag::Category>(i));
    }
}

void test_enumerate_categories_order(Tally& checks) {
    // The count alone would pass for a visitor that visited one entry
    // many times, so the walk is repeated for its order: each visit is the
    // category at the position of the visit, and the names of the first and
    // the last visit are the names of the two end tags.
    TextView first_visited{};
    TextView last_visited{};
    std::size_t cursor = 0;
    std::size_t misplaced = 0;
    diag::enumerate_categories([&first_visited, &last_visited, &cursor, &misplaced]<diag::Category C>() noexcept {
        if (cursor >= diag::catalog_size || diag::categories_v[cursor] != C) ++misplaced;
        if (cursor == 0) first_visited = diag::name_of(C);
        last_visited = diag::name_of(C);
        ++cursor;
    });
    CRUCIBLE_TEST_EXPECT(checks, misplaced == 0);
    CRUCIBLE_TEST_EXPECT(checks, cursor == diag::catalog_size);
    CRUCIBLE_TEST_EXPECT(checks, first_visited == LeadingTag::name);
    CRUCIBLE_TEST_EXPECT(checks, last_visited == TrailingTag::name);
}

// The accessors index arrays derived from the tuple.  Every category is
// walked rather than three named ones, so an array built in the wrong
// order, or a tag whose fields drifted from its accessors, shows in the count
// of its field.
void test_accessor_strings_match_tag_fields(Tally& checks) {
    std::size_t name_drifts = 0;
    std::size_t description_drifts = 0;
    std::size_t remediation_drifts = 0;
    diag::enumerate_categories([&name_drifts, &description_drifts, &remediation_drifts]<diag::Category C>() noexcept {
        using tag = diag::tag_of_t<C>;
        if (diag::name_of(C) != tag::name) ++name_drifts;
        if (diag::description_of(C) != tag::description) ++description_drifts;
        if (diag::remediation_of(C) != tag::remediation) ++remediation_drifts;
    });
    CRUCIBLE_TEST_EXPECT(checks, name_drifts == 0);
    CRUCIBLE_TEST_EXPECT(checks, description_drifts == 0);
    CRUCIBLE_TEST_EXPECT(checks, remediation_drifts == 0);
    CRUCIBLE_TEST_EXPECT(checks, diag::name_of(diag::categories_v.back()) == TrailingTag::name);
}

// The tuple is derived from the enum by identifier: the tag at every
// index spells the enumerator at that index.
[[nodiscard]] consteval bool catalog_spells_the_enum() noexcept {
    static constexpr ::foundation::reflect::ReflectionList enumerators =
        ::foundation::reflect::reflection_list(std::meta::enumerators_of(^^diag::Category));
    std::size_t position = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr diag::Category c = [:en:];
        // The identifier of a reflection has static storage and a zero after
        // its last character, so the static text door gives it as a TextView.
        if (::fixy::mint_static_text(std::meta::identifier_of(en).data()) != diag::tag_of_t<c>::name) return false;
        // tag_of_t is an alias, and an alias has no identifier of its own.
        if (std::meta::identifier_of(en) != std::meta::identifier_of(std::meta::dealias(^^diag::tag_of_t<c>))) {
            return false;
        }
        if (static_cast<std::size_t>(std::to_underlying(c)) != position) return false;
        ++position;
    }
#pragma GCC diagnostic pop
    return position == diag::catalog_size;
}

static_assert(catalog_spells_the_enum());

}  // namespace

// ── Insights: the three registration forms, as a consumer writes them ────

namespace user_proj::diag_tags {

// A tag must inherit tag_base to be registrable.
struct PaymentRefundLeak : ::foundation::diag::tag_base {
    static constexpr ::foundation::diag::tag_text_literal name = "PaymentRefundLeak";
    static constexpr ::foundation::diag::tag_text_literal description =
        "user-defined: payment service refunded a transaction without "
        "rolling back the parent order's revenue accrual";
    static constexpr ::foundation::diag::tag_text_literal remediation =
        "Use TwoPhaseRefund<...> or include the refund in the "
        "original transaction's atomic boundary.";
};

// A tag whose prose is still pending but whose severity already matters, so
// it is registered through the severity-only form.
struct ScheduleDoubleAcceleration : ::foundation::diag::tag_base {
    static constexpr ::foundation::diag::tag_text_literal name = "ScheduleDoubleAcceleration";
    static constexpr ::foundation::diag::tag_text_literal description =
        "user-defined: a scheduled event was accelerated twice in the "
        "same window — surfaces in finance / ops";
    static constexpr ::foundation::diag::tag_text_literal remediation =
        "Use idempotent acceleration tokens (TODO: extract into combinator).";
};

// A tag whose every prose field clears the quality minimums.
struct CrossTenantLeak : ::foundation::diag::tag_base {
    static constexpr ::foundation::diag::tag_text_literal name = "CrossTenantLeak";
    static constexpr ::foundation::diag::tag_text_literal description =
        "user-defined: a query response from tenant A surfaced data "
        "from tenant B's table — multi-tenant isolation breach";
    static constexpr ::foundation::diag::tag_text_literal remediation =
        "Wrap query handles in TenantScoped<TenantId, T>; refuse "
        "construction across tenant boundaries via Refined<>.";
};

}  // namespace user_proj::diag_tags

// Every field populated, which is the ordinary registration shape.
CRUCIBLE_DIAG_INSIGHTS(::user_proj::diag_tags::PaymentRefundLeak, ::foundation::diag::Severity::Error,
                       "Refunds without parent-order rollback corrupt revenue reporting "
                       "and cause month-end reconciliation drift.  The accounting layer's "
                       "TwoPhaseRefund<...> primitive is the correct vehicle.",
                       "Surfaces as month-end revenue mismatch; the refund line items "
                       "exist in the refund ledger but the parent revenue accrual still "
                       "shows as recognized.",
                       "TwoPhaseRefund<TxId>(parent_id).commit_with_revenue_rollback();",
                       "RefundService::refund(parent_id);  // VIOLATES — no rollback");

// Severity without prose.  Adding the prose later means replacing this
// specialization rather than writing a second one alongside it.
CRUCIBLE_DIAG_INSIGHTS_SEVERITY(::user_proj::diag_tags::ScheduleDoubleAcceleration,
                                ::foundation::diag::Severity::Fatal);

// The quality-validated form holds each prose field to a minimum length: 30
// characters for the reason, 20 for the symptom, 10 for each example.  A
// placeholder left in by accident fails to compile.
CRUCIBLE_DIAG_INSIGHTS_QV(::user_proj::diag_tags::CrossTenantLeak, ::foundation::diag::Severity::Fatal,
                          "A multi-tenant isolation breach is a security incident.  Fixing "
                          "after the fact requires customer notification, audit-trail "
                          "review, and (depending on jurisdiction) regulatory disclosure.",
                          "Surfaces as a query result that includes rows from a different "
                          "tenant_id than the requestor's session.",
                          "TenantScoped<TenantId, Result> r = query(scope, ...);",
                          "Result r = raw_query(...);  // VIOLATES — bypasses TenantScoped");

namespace user_proj::diag_tags::self_test {

using Pfull = diag::insight_provider<PaymentRefundLeak>;
static_assert(Pfull::severity == diag::Severity::Error);
static_assert(::fixy::starts_with(Pfull::why_this_matters, "Refunds without"));
static_assert(::fixy::find(Pfull::violating_example, "VIOLATES").is_some());

using Psev = diag::insight_provider<ScheduleDoubleAcceleration>;
static_assert(Psev::severity == diag::Severity::Fatal);
static_assert(Psev::why_this_matters.size() == 0);
static_assert(Psev::correct_example.size() == 0);

using Pqv = diag::insight_provider<CrossTenantLeak>;
static_assert(Pqv::severity == diag::Severity::Fatal);
static_assert(Pqv::why_this_matters.size() >= diag::insights_quality_thresholds<CrossTenantLeak>::min_why_chars);
static_assert(Pqv::symptom_pattern.size() >= diag::insights_quality_thresholds<CrossTenantLeak>::min_symptom_chars);

static_assert(diag::has_insights_v<PaymentRefundLeak>);

// A severity-only registration reports no insights, because all four prose
// fields are empty.  That is the acknowledged-but-unwritten state, and it is
// deliberately distinguishable from a tag nobody has registered at all.
static_assert(!diag::has_insights_v<ScheduleDoubleAcceleration>);

static_assert(diag::has_insights_v<CrossTenantLeak>);
static_assert(diag::has_substantive_insights_v<CrossTenantLeak>);

static_assert(diag::WellInsightedTag<PaymentRefundLeak>);
static_assert(!diag::WellInsightedTag<ScheduleDoubleAcceleration>);
static_assert(diag::WellInsightedTag<CrossTenantLeak>);

// The substantive gate is the stronger of the two.
static_assert(!diag::HasSubstantiveInsights<PaymentRefundLeak> || diag::has_substantive_insights_v<PaymentRefundLeak>);
static_assert(diag::HasSubstantiveInsights<CrossTenantLeak>);

static_assert(!diag::WellInsightedTag<int>);
static_assert(!diag::WellInsightedTag<void>);

// An unregistered tag carries the empty defaults and Error.
static_assert(!diag::has_insights_v<::anonymous_local_tag>);
static_assert(diag::insight_provider<::anonymous_local_tag>::severity == diag::Severity::Error);

}  // namespace user_proj::diag_tags::self_test

namespace {

// Reading the accessors at runtime forces the instantiations into the
// binary, which nothing above does.
void test_insights_runtime(Tally& checks) {
    auto sev_full = diag::insight_provider<::user_proj::diag_tags::PaymentRefundLeak>::severity;
    auto sev_qv = diag::insight_provider<::user_proj::diag_tags::CrossTenantLeak>::severity;
    CRUCIBLE_TEST_EXPECT(checks, diag::severity_name(sev_full) == "Error");
    CRUCIBLE_TEST_EXPECT(checks, diag::severity_name(sev_qv) == "Fatal");

    // The bound is the enumerator count read off the enum, so a new
    // severity is covered without an edit here; the enumerators are
    // consecutive from zero, which the loop's cast relies on.
    volatile std::size_t const cap = std::meta::enumerators_of(^^diag::Severity).size();
    for (std::size_t i = 0; i < cap; ++i) {
        CRUCIBLE_TEST_EXPECT(checks, diag::severity_name(static_cast<diag::Severity>(i)).size() != 0);
        CRUCIBLE_TEST_EXPECT(checks, diag::severity_name(static_cast<diag::Severity>(i)) != "<unknown Severity>");
    }
    CRUCIBLE_TEST_EXPECT(checks, diag::severity_name(static_cast<diag::Severity>(9)) == "<unknown Severity>");
}

// ── Runtime: the sink protocol, through a capturing sink ─────────────────

struct CapturedCall {
    ::fixy::Atomic<std::uint32_t> count{0};
    diag::Category last_cat{diag::Category::EffectRowMismatch};
    // The runtime formats "<file>:<line>:<column>@<function>" into 2048
    // bytes. A shorter buffer cuts the function name off in a deep checkout.
    ::fixy::FixedText<2048> last_fn{};
    ::fixy::FixedText<256> last_detail{};
};

CapturedCall g_captured{};

// Copies the characters of source into target, up to the capacity of
// target.  A longer source keeps its first characters.
template <std::size_t Capacity>
void copy_bounded(::fixy::FixedText<Capacity>& target, TextView source) noexcept {
    target.clear();
    for (char const character : source) {
        if (target.append(character).is_err()) return;
    }
}

void capture_sink(diag::Category cat, diag::ViolationContextText const& fn,
                  diag::ViolationDetailText const& detail) noexcept {
    static_cast<void>(g_captured.count.fetch_add_acq_rel(1));
    g_captured.last_cat = cat;
    copy_bounded(g_captured.last_fn, fn.value());
    copy_bounded(g_captured.last_detail, detail.value());
}

void reset_capture() noexcept {
    g_captured.count.store_release(0);
    g_captured.last_cat = diag::Category::EffectRowMismatch;
    g_captured.last_fn.clear();
    g_captured.last_detail.clear();
}

void test_default_sink_installed(Tally& checks) {
    CRUCIBLE_TEST_EXPECT(checks, diag::current_violation_sink() == &diag::default_violation_sink);
}

void test_custom_sink_install_and_route(Tally& checks) {
    auto previous = diag::set_violation_sink(&capture_sink);
    CRUCIBLE_TEST_EXPECT(checks, previous == &diag::default_violation_sink);
    CRUCIBLE_TEST_EXPECT(checks, diag::current_violation_sink() == &capture_sink);

    reset_capture();
    diag::report_violation(diag::Category::DetSafeLeak, diag::ViolationContextText{"test_function"},
                           diag::ViolationDetailText{"value=42 fails determinism predicate"});

    CRUCIBLE_TEST_EXPECT(checks, g_captured.count.load_acquire() == 1);
    CRUCIBLE_TEST_EXPECT(checks, g_captured.last_cat == diag::Category::DetSafeLeak);
    CRUCIBLE_TEST_EXPECT(checks, g_captured.last_fn.view() == "test_function");
    CRUCIBLE_TEST_EXPECT(checks, g_captured.last_detail.view() == "value=42 fails determinism predicate");

    diag::set_violation_sink(previous);
    CRUCIBLE_TEST_EXPECT(checks, diag::current_violation_sink() == &diag::default_violation_sink);
}

void test_null_sink_drops_silently(Tally& checks) {
    auto previous = diag::set_violation_sink(nullptr);
    CRUCIBLE_TEST_EXPECT(checks, diag::current_violation_sink() == nullptr);

    reset_capture();
    diag::report_violation(diag::Category::HotPathViolation, diag::ViolationContextText{"drop_test"},
                           diag::ViolationDetailText{"should be silently dropped"});
    CRUCIBLE_TEST_EXPECT(checks, g_captured.count.load_acquire() == 0);

    diag::set_violation_sink(previous);
}

// No Category may be special-cased out of the emitter.
void test_all_categories_route_through_sink(Tally& checks) {
    auto previous = diag::set_violation_sink(&capture_sink);

    // The walk hands each Category to the lambda as a non-type template
    // parameter, which is why the lambda is spelled with one.
    std::size_t seen = 0;
    diag::enumerate_categories([&]<diag::Category C>() noexcept {
        reset_capture();
        diag::report_violation(C, diag::ViolationContextText{"enum_test"},
                               diag::ViolationDetailText{"category enumeration walk"});
        if (g_captured.count.load_acquire() == 1 && g_captured.last_cat == C) {
            ++seen;
        }
    });
    CRUCIBLE_TEST_EXPECT(checks, seen == diag::catalog_size);

    diag::set_violation_sink(previous);
}

void test_long_input_reaches_sink_once(Tally& checks) {
    constexpr std::size_t big_n = 8192;
    ::fixy::FixedText<big_n> big{};
    bool is_filled = true;
    for (std::size_t index = 0; index < big_n; ++index) {
        is_filled = is_filled && big.append('x').is_ok();
    }
    CRUCIBLE_TEST_EXPECT(checks, is_filled && big.size() == big_n);

    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();
    diag::report_violation(diag::Category::BudgetExceeded, diag::ViolationContextText{"long_detail_test"},
                           diag::ViolationDetailText{big.view()});
    CRUCIBLE_TEST_EXPECT(checks, g_captured.count.load_acquire() == 1);
    diag::set_violation_sink(previous);
}

void test_empty_strings_handled(Tally& checks) {
    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();
    diag::report_violation(diag::Category::EpochMismatch, diag::ViolationContextText{TextView{}},
                           diag::ViolationDetailText{TextView{}});
    CRUCIBLE_TEST_EXPECT(checks, g_captured.count.load_acquire() == 1);
    CRUCIBLE_TEST_EXPECT(checks, g_captured.last_fn.view() == "");
    CRUCIBLE_TEST_EXPECT(checks, g_captured.last_detail.view() == "");
    diag::set_violation_sink(previous);
}

// This covers the exchange semantic only.  Race detection belongs to the
// sanitizer preset.
void test_sink_install_round_trips(Tally& checks) {
    for (int i = 0; i < 100; ++i) {
        auto p1 = diag::set_violation_sink(&capture_sink);
        auto p2 = diag::set_violation_sink(p1);
        CRUCIBLE_TEST_EXPECT(checks, p2 == &capture_sink);
        CRUCIBLE_TEST_EXPECT(checks, diag::current_violation_sink() == p1);
    }
}

void test_report_violation_at_captures_location(Tally& checks) {
    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();

    diag::report_violation_at(diag::Category::RefinementViolation, "value=-7 fails predicate `positive`");
    CRUCIBLE_TEST_EXPECT(checks, g_captured.count.load_acquire() == 1);

    // The captured name field carries "<file>:<line>:<column>@<function>",
    // and the JSON emitter's parser reads it back.
    TextView const fn = g_captured.last_fn.view();
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::find(fn, "test_diag.cpp").is_some());
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::find(fn, '@').is_some());
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::find(fn, "test_report_violation_at_captures_location").is_some());

    const diag::SourcePosition pos = diag::parse_source_position(fn);
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::ends_with(pos.file, "test_diag.cpp"));
    CRUCIBLE_TEST_EXPECT(checks, pos.line > 0);
    CRUCIBLE_TEST_EXPECT(checks, pos.column > 0);
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::find(pos.function, "test_report_violation_at_captures_location").is_some());

    diag::set_violation_sink(previous);
}

void test_macro_captures_location(Tally& checks) {
    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();

    CRUCIBLE_DIAG_RUNTIME_VIOLATION(diag::Category::HotPathViolation, "macro test detail");

    CRUCIBLE_TEST_EXPECT(checks, g_captured.count.load_acquire() == 1);
    CRUCIBLE_TEST_EXPECT(checks, g_captured.last_cat == diag::Category::HotPathViolation);
    CRUCIBLE_TEST_EXPECT(checks, g_captured.last_detail.view() == "macro test detail");
    CRUCIBLE_TEST_EXPECT(checks, ::fixy::find(g_captured.last_fn.view(), "test_macro_captures_location").is_some());

    diag::set_violation_sink(previous);
}

// The aborting emitters reach the sink first, then abort.  Each emitter runs
// in a child through expect_dies of the Check family.  The child keeps the
// default sink, which writes the category, the name and the detail of the
// violation to standard error, and the parent reads that line.
void test_abort_emitters_reach_sink_then_abort(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            diag::report_violation_and_abort(diag::Category::CrashClassMismatch, diag::ViolationContextText{"abort_fn"},
                                             diag::ViolationDetailText{"abort detail"});
        },
        ::foundation::core::TextView{
            "crucible-violation: category=CrashClassMismatch fn=abort_fn detail=abort detail\n"});

    ::fixy::check::expect_dies(
        test, [] noexcept { diag::report_violation_at_and_abort(diag::Category::LinearityViolation, "at and abort"); },
        ::foundation::core::TextView{
            "crucible-violation: category=LinearityViolation fn=test/foundation/test_diag.cpp:"});

    // The position of the macro sits between the category and the detail, so
    // one child shows the category and a second child shows the detail.
    auto const macro_abort = [] noexcept {
        CRUCIBLE_DIAG_RUNTIME_VIOLATION_AND_ABORT(diag::Category::WaitStrategyViolation, "macro abort");
    };
    ::fixy::check::expect_dies(
        test, macro_abort,
        ::foundation::core::TextView{
            "crucible-violation: category=WaitStrategyViolation fn=test/foundation/test_diag.cpp:"});
    ::fixy::check::expect_dies(test, macro_abort, ::foundation::core::TextView{" detail=macro abort\n"});
}

// ── Runtime: the output shapes ───────────────────────────────────────────
//
// test/harness/probe_diag.cpp writes each record and each line of the
// emitter, and test/harness/check_output.py compares their bytes.  This call
// writes the line of the default sink to the standard error of this process.
void test_default_sink_emits_text() {
    diag::default_violation_sink(diag::Category::DetSafeLeak,
                                 diag::ViolationContextText{"test_default_sink_emits_text"},
                                 diag::ViolationDetailText{"expected line"});
}

// The violation record is the composite context taken apart plus the
// catalog's prose for the category.  test/harness/check_output.py compares
// the bytes of the record in the fixed field order.
void test_json_violation_record_shape(Tally& checks) {
    constexpr diag::Category cat = diag::Category::EffectRowMismatch;
    const diag::JsonDiagnosticRecord rec =
        diag::record_from_violation(cat, diag::ViolationContextText{"plain_fn"}, diag::ViolationDetailText{"gap"});
    CRUCIBLE_TEST_EXPECT(checks, rec.category == cat);
    CRUCIBLE_TEST_EXPECT(checks, rec.source.file.size() == 0);
    CRUCIBLE_TEST_EXPECT(checks, rec.source.function == "plain_fn");
    CRUCIBLE_TEST_EXPECT(checks, rec.have == "plain_fn");
    CRUCIBLE_TEST_EXPECT(checks, rec.error_code == diag::name_of(cat));
    CRUCIBLE_TEST_EXPECT(checks, rec.goal == diag::description_of(cat));
    CRUCIBLE_TEST_EXPECT(checks, rec.suggestion == diag::remediation_of(cat));
}

void test_source_position_parser(Tally& checks) {
    const auto pos = diag::parse_source_position("/tmp/project/test.cpp:42:7@void ns::fn()");
    CRUCIBLE_TEST_EXPECT(checks, pos.file == "/tmp/project/test.cpp");
    CRUCIBLE_TEST_EXPECT(checks, pos.line == 42);
    CRUCIBLE_TEST_EXPECT(checks, pos.column == 7);
    CRUCIBLE_TEST_EXPECT(checks, pos.function == "void ns::fn()");

    const auto legacy = diag::parse_source_position("plain_function");
    CRUCIBLE_TEST_EXPECT(checks, legacy.file.size() == 0);
    CRUCIBLE_TEST_EXPECT(checks, legacy.function == "plain_function");

    const auto line_only = diag::parse_source_position("only.cpp:12@fn");
    CRUCIBLE_TEST_EXPECT(checks, line_only.file == "only.cpp");
    CRUCIBLE_TEST_EXPECT(checks, line_only.line == 12);
    CRUCIBLE_TEST_EXPECT(checks, line_only.column == 0);

    const auto oversized = diag::parse_source_position("overflow.cpp:999999999999999999999999:7@overflow_fn");
    CRUCIBLE_TEST_EXPECT(checks, oversized.file == "overflow.cpp:999999999999999999999999:7");
    CRUCIBLE_TEST_EXPECT(checks, oversized.line == 0);

    const auto empty = diag::parse_source_position("");
    CRUCIBLE_TEST_EXPECT(checks, empty.file.size() == 0);
    CRUCIBLE_TEST_EXPECT(checks, empty.function.size() == 0);
}

}  // namespace

int main() {
    Tally checks{kTest, "test_diag"};
    test_runtime_smoke();
    test_catalog_floor(checks);
    test_accessor_runtime_coverage(checks);
    test_categories_array(checks);
    test_enumerate_categories_order(checks);
    test_accessor_strings_match_tag_fields(checks);
    test_insights_runtime(checks);

    test_default_sink_installed(checks);
    test_custom_sink_install_and_route(checks);
    test_null_sink_drops_silently(checks);
    test_all_categories_route_through_sink(checks);
    test_long_input_reaches_sink_once(checks);
    test_empty_strings_handled(checks);
    test_sink_install_round_trips(checks);
    test_report_violation_at_captures_location(checks);
    test_macro_captures_location(checks);
    test_abort_emitters_reach_sink_then_abort(kTest);

    test_default_sink_emits_text();
    test_json_violation_record_shape(checks);
    test_source_position_parser(checks);

    std::move(checks).close();
    return 0;
}
