// Sentinel TU for foundation/diag: the catalog's own checks compile under
// the project warning set, the accessors run with non-constant arguments,
// the three insight registration forms expand the way a consumer writes
// them, and the runtime sink is driven in both output shapes.

#include <foundation/diag/Catalog.h>
#include <foundation/diag/Insights.h>
#include <foundation/diag/JsonEmitter.h>
#include <foundation/diag/Runtime.h>

#include "abort_probe.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace diag = ::foundation::diag;

namespace {

int g_failures = 0;

// Prints the failing expression and its position, and counts it.  A cell
// keeps going after a failure so one run reports every failure it has.
#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

// ── Catalog: compile-time cells ──────────────────────────────────────────

// The first and last tags are read out of the catalog rather than named
// here.  Naming one would make every append to the catalog redden this
// file, which tells the appender nothing about what actually broke.
using LeadingTag = std::tuple_element_t<0, diag::Catalog>;
using TrailingTag = std::tuple_element_t<diag::catalog_size - 1, diag::Catalog>;

static_assert(diag::category_of_v<LeadingTag> == diag::categories_v.front());
static_assert(diag::category_of_v<TrailingTag> == diag::categories_v.back());
static_assert(std::is_same_v<diag::tag_of_t<diag::categories_v.back()>, TrailingTag>);

// The enum is walked by reflection in the header; the count it derives
// is the tuple's size, and it is in scope here as the header's own witness.
static_assert(diag::detail::diag_self_test::category_count == diag::catalog_size);

static_assert(diag::category_of_v<diag::EffectRowMismatch> == diag::Category::EffectRowMismatch);
static_assert(diag::category_of_v<diag::DetSafeLeak> == diag::Category::DetSafeLeak);
static_assert(diag::category_of_v<diag::RecipeSpecMismatch> == diag::Category::RecipeSpecMismatch);

static_assert(std::is_same_v<diag::tag_of_t<diag::Category::EffectRowMismatch>, diag::EffectRowMismatch>);
static_assert(std::is_same_v<diag::tag_of_t<diag::Category::HotPathViolation>, diag::HotPathViolation>);

// The fixture lives at namespace scope because a function-local class
// may not have static data members, and a tag is all static members.
struct anonymous_local_tag : diag::tag_base {
    static constexpr std::string_view name = "AnonymousLocalTag";
    static constexpr std::string_view description = "test fixture";
    static constexpr std::string_view remediation = "rebuild";
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

// The header's own checks are all constant-evaluated.  This one runs
// the same accessors with non-constant arguments, which is where an
// inline-body defect in a switch would surface.
void test_runtime_smoke() { diag::runtime_smoke_test(); }

// The bound is a floor, not an equality.  The catalog is append-only, so
// this guards against it shrinking; the header derives the exact count
// from the enum and pins it to the tuple itself.
void test_catalog_floor() { EXPECT(diag::catalog_size >= std::size_t{33}); }

void test_accessor_runtime_coverage() {
    // The volatile bound stops the loop folding away, so each accessor
    // is entered with a value the compiler cannot see through.
    volatile std::size_t const cap = diag::catalog_size;
    constexpr std::string_view sentinel{"<unknown Category>"};
    for (std::size_t i = 0; i < cap; ++i) {
        diag::Category const c = static_cast<diag::Category>(i);
        std::string_view const n = diag::name_of(c);
        std::string_view const d = diag::description_of(c);
        std::string_view const r = diag::remediation_of(c);
        EXPECT(!n.empty());
        EXPECT(!d.empty());
        EXPECT(!r.empty());
        EXPECT(n != sentinel);
        EXPECT(d != sentinel);
        EXPECT(r != sentinel);
    }

    // A value outside the enumeration reaches the default arm and comes
    // back as the sentinel.  Only a cast can produce such a value, which
    // is why the test has to make one by hand.
    diag::Category const bogus = static_cast<diag::Category>(255);
    EXPECT(diag::name_of(bogus) == sentinel);
    EXPECT(diag::description_of(bogus) == sentinel);
    EXPECT(diag::remediation_of(bogus) == sentinel);
}

void test_categories_array() {
    EXPECT(diag::categories_v.size() == diag::catalog_size);
    EXPECT(diag::categories_v[0] == diag::category_of_v<LeadingTag>);
    EXPECT(diag::categories_v[diag::catalog_size - 1] == diag::category_of_v<TrailingTag>);

    // The array index and the enumerator value have to stay the same
    // number, because the accessors above index by enumerator.
    volatile std::size_t const cap = diag::catalog_size;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT(diag::categories_v[i] == static_cast<diag::Category>(i));
    }
}

void test_enumerate_categories_order() {
    // The count alone would pass for a visitor that visited one entry
    // many times, so the walk is repeated for its order.
    std::array<std::string_view, diag::catalog_size> visited{};
    std::size_t cursor = 0;
    diag::enumerate_categories(
        [&visited, &cursor]<diag::Category C>() noexcept { visited[cursor++] = diag::name_of(C); });
    EXPECT(cursor == diag::catalog_size);
    EXPECT(visited.front() == LeadingTag::name);
    EXPECT(visited.back() == TrailingTag::name);
}

// The accessors are switches written by hand alongside the tags.  These
// comparisons are what catch an arm that answers for the wrong tag.
void test_accessor_strings_match_tag_fields() {
    EXPECT(diag::name_of(diag::Category::EffectRowMismatch) == diag::EffectRowMismatch::name);
    EXPECT(diag::name_of(diag::Category::DetSafeLeak) == diag::DetSafeLeak::name);
    EXPECT(diag::name_of(diag::Category::RecipeSpecMismatch) == diag::RecipeSpecMismatch::name);
    EXPECT(diag::description_of(diag::Category::DetSafeLeak) == diag::DetSafeLeak::description);
    EXPECT(diag::remediation_of(diag::Category::DetSafeLeak) == diag::DetSafeLeak::remediation);
    EXPECT(diag::name_of(diag::categories_v.back()) == TrailingTag::name);
}

}  // namespace

// ── Insights: the three registration forms, as a consumer writes them ────

namespace user_proj::diag_tags {

// A tag must inherit tag_base to be registrable.
struct PaymentRefundLeak : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "PaymentRefundLeak";
    static constexpr std::string_view description = "user-defined: payment service refunded a transaction without "
                                                    "rolling back the parent order's revenue accrual";
    static constexpr std::string_view remediation = "Use TwoPhaseRefund<...> or include the refund in the "
                                                    "original transaction's atomic boundary.";
};

// A tag whose prose is still pending but whose severity already matters, so
// it is registered through the severity-only form.
struct ScheduleDoubleAcceleration : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "ScheduleDoubleAcceleration";
    static constexpr std::string_view description = "user-defined: a scheduled event was accelerated twice in the "
                                                    "same window — surfaces in finance / ops";
    static constexpr std::string_view remediation =
        "Use idempotent acceleration tokens (TODO: extract into combinator).";
};

// A tag whose every prose field clears the quality minimums.
struct CrossTenantLeak : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "CrossTenantLeak";
    static constexpr std::string_view description = "user-defined: a query response from tenant A surfaced data "
                                                    "from tenant B's table — multi-tenant isolation breach";
    static constexpr std::string_view remediation = "Wrap query handles in TenantScoped<TenantId, T>; refuse "
                                                    "construction across tenant boundaries via Refined<>.";
};

}  // namespace user_proj::diag_tags

// Every field populated, which is the ordinary registration shape.
CRUCIBLE_DEFINE_INSIGHTS(::user_proj::diag_tags::PaymentRefundLeak, ::foundation::diag::Severity::Error,
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
CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(::user_proj::diag_tags::ScheduleDoubleAcceleration,
                                  ::foundation::diag::Severity::Fatal);

// The quality-validated form holds each prose field to a minimum length: 30
// characters for the reason, 20 for the symptom, 10 for each example.  A
// placeholder left in by accident fails to compile.
CRUCIBLE_DEFINE_INSIGHTS_QV(::user_proj::diag_tags::CrossTenantLeak, ::foundation::diag::Severity::Fatal,
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
static_assert(Pfull::why_this_matters.starts_with("Refunds without"));
static_assert(Pfull::violating_example.find("VIOLATES") != std::string_view::npos);

using Psev = diag::insight_provider<ScheduleDoubleAcceleration>;
static_assert(Psev::severity == diag::Severity::Fatal);
static_assert(Psev::why_this_matters.empty());
static_assert(Psev::correct_example.empty());

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
void test_insights_runtime() {
    auto sev_full = diag::insight_provider<::user_proj::diag_tags::PaymentRefundLeak>::severity;
    auto sev_qv = diag::insight_provider<::user_proj::diag_tags::CrossTenantLeak>::severity;
    EXPECT(diag::severity_name(sev_full) == "Error");
    EXPECT(diag::severity_name(sev_qv) == "Fatal");

    volatile std::size_t const cap = 4;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT(!diag::severity_name(static_cast<diag::Severity>(i)).empty());
    }
    EXPECT(diag::severity_name(static_cast<diag::Severity>(9)) == "<unknown Severity>");
}

// ── Runtime: the sink protocol, through a capturing sink ─────────────────

struct CapturedCall {
    std::atomic<int> count{0};
    diag::Category last_cat{diag::Category::EffectRowMismatch};
    char last_fn[512]{};
    char last_detail[256]{};
};

CapturedCall g_captured{};

void capture_sink(diag::Category cat, std::string_view fn, std::string_view detail) noexcept {
    g_captured.count.fetch_add(1, std::memory_order_relaxed);
    g_captured.last_cat = cat;

    // An empty string_view may carry a null data pointer, and passing
    // that to memcpy is undefined even for a length of zero.
    auto bounded_copy = [](char* dst, std::size_t cap, std::string_view src) noexcept {
        const std::size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
        if (n > 0 && src.data() != nullptr) {
            std::memcpy(dst, src.data(), n);
        }
        dst[n] = '\0';
    };
    bounded_copy(g_captured.last_fn, sizeof(g_captured.last_fn), fn);
    bounded_copy(g_captured.last_detail, sizeof(g_captured.last_detail), detail);
}

void reset_capture() noexcept {
    g_captured.count.store(0, std::memory_order_relaxed);
    g_captured.last_cat = diag::Category::EffectRowMismatch;
    g_captured.last_fn[0] = '\0';
    g_captured.last_detail[0] = '\0';
}

void test_default_sink_installed() { EXPECT(diag::current_violation_sink() == &diag::default_violation_sink); }

void test_custom_sink_install_and_route() {
    auto previous = diag::set_violation_sink(&capture_sink);
    EXPECT(previous == &diag::default_violation_sink);
    EXPECT(diag::current_violation_sink() == &capture_sink);

    reset_capture();
    diag::report_violation(diag::Category::DetSafeLeak, "test_function", "value=42 fails determinism predicate");

    EXPECT(g_captured.count.load() == 1);
    EXPECT(g_captured.last_cat == diag::Category::DetSafeLeak);
    EXPECT(std::string_view{g_captured.last_fn} == "test_function");
    EXPECT(std::string_view{g_captured.last_detail} == "value=42 fails determinism predicate");

    diag::set_violation_sink(previous);
    EXPECT(diag::current_violation_sink() == &diag::default_violation_sink);
}

void test_null_sink_drops_silently() {
    auto previous = diag::set_violation_sink(nullptr);
    EXPECT(diag::current_violation_sink() == nullptr);

    reset_capture();
    diag::report_violation(diag::Category::HotPathViolation, "drop_test", "should be silently dropped");
    EXPECT(g_captured.count.load() == 0);

    diag::set_violation_sink(previous);
}

// No Category may be special-cased out of the emitter.
void test_all_categories_route_through_sink() {
    auto previous = diag::set_violation_sink(&capture_sink);

    // The walk hands each Category to the lambda as a non-type template
    // parameter, which is why the lambda is spelled with one.
    std::size_t seen = 0;
    diag::enumerate_categories([&]<diag::Category C>() noexcept {
        reset_capture();
        diag::report_violation(C, "enum_test", "category enumeration walk");
        if (g_captured.count.load() == 1 && g_captured.last_cat == C) {
            ++seen;
        }
    });
    EXPECT(seen == diag::catalog_size);

    diag::set_violation_sink(previous);
}

void test_long_input_reaches_sink_once() {
    constexpr std::size_t big_n = 8192;
    char big[big_n + 1];
    std::memset(big, 'x', big_n);
    big[big_n] = '\0';

    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();
    diag::report_violation(diag::Category::BudgetExceeded, "long_detail_test", std::string_view{big, big_n});
    EXPECT(g_captured.count.load() == 1);
    diag::set_violation_sink(previous);
}

void test_empty_strings_handled() {
    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();
    diag::report_violation(diag::Category::EpochMismatch, {}, {});
    EXPECT(g_captured.count.load() == 1);
    EXPECT(std::string_view{g_captured.last_fn} == "");
    EXPECT(std::string_view{g_captured.last_detail} == "");
    diag::set_violation_sink(previous);
}

// This covers the exchange semantic only.  Race detection belongs to the
// sanitizer preset.
void test_sink_install_round_trips() {
    for (int i = 0; i < 100; ++i) {
        auto p1 = diag::set_violation_sink(&capture_sink);
        auto p2 = diag::set_violation_sink(p1);
        EXPECT(p2 == &capture_sink);
        EXPECT(diag::current_violation_sink() == p1);
    }
}

void test_report_violation_at_captures_location() {
    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();

    diag::report_violation_at(diag::Category::RefinementViolation, "value=-7 fails predicate `positive`");
    EXPECT(g_captured.count.load() == 1);

    // The captured name field carries "<file>:<line>:<column>@<function>",
    // and the JSON emitter's parser reads it back.
    std::string_view fn{g_captured.last_fn};
    EXPECT(fn.find("test_diag.cpp") != std::string_view::npos);
    EXPECT(fn.find('@') != std::string_view::npos);
    EXPECT(fn.find("test_report_violation_at_captures_location") != std::string_view::npos);

    const diag::SourcePosition pos = diag::parse_source_position(fn);
    EXPECT(pos.file.ends_with("test_diag.cpp"));
    EXPECT(pos.line > 0);
    EXPECT(pos.column > 0);
    EXPECT(pos.function.find("test_report_violation_at_captures_location") != std::string_view::npos);

    diag::set_violation_sink(previous);
}

void test_macro_captures_location() {
    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();

    CRUCIBLE_RUNTIME_VIOLATION(diag::Category::HotPathViolation, "macro test detail");

    EXPECT(g_captured.count.load() == 1);
    EXPECT(g_captured.last_cat == diag::Category::HotPathViolation);
    EXPECT(std::string_view{g_captured.last_detail} == "macro test detail");
    EXPECT(std::string_view{g_captured.last_fn}.find("test_macro_captures_location") != std::string_view::npos);

    diag::set_violation_sink(previous);
}

// The aborting emitters reach the sink first, then abort.
void test_abort_emitters_reach_sink_then_abort() {
    using foundation::test::aborts;
    auto previous = diag::set_violation_sink(&capture_sink);

    reset_capture();
    EXPECT(aborts(
        [] { diag::report_violation_and_abort(diag::Category::CrashClassMismatch, "abort_fn", "abort detail"); }));
    EXPECT(g_captured.count.load() == 1);
    EXPECT(g_captured.last_cat == diag::Category::CrashClassMismatch);
    EXPECT(std::string_view{g_captured.last_fn} == "abort_fn");

    reset_capture();
    EXPECT(aborts([] { diag::report_violation_at_and_abort(diag::Category::LinearityViolation, "at and abort"); }));
    EXPECT(g_captured.count.load() == 1);
    EXPECT(g_captured.last_cat == diag::Category::LinearityViolation);
    EXPECT(std::string_view{g_captured.last_fn}.find('@') != std::string_view::npos);

    reset_capture();
    EXPECT(aborts([] { CRUCIBLE_RUNTIME_VIOLATION_AND_ABORT(diag::Category::WaitStrategyViolation, "macro abort"); }));
    EXPECT(g_captured.count.load() == 1);
    EXPECT(g_captured.last_cat == diag::Category::WaitStrategyViolation);
    EXPECT(std::string_view{g_captured.last_detail} == "macro abort");

    diag::set_violation_sink(previous);
}

// ── Runtime: the two output shapes, through file-backed sinks ────────────

[[nodiscard]] std::string read_tmp_file(std::FILE* f) {
    if (std::fflush(f) != 0) return {};
    if (std::fseek(f, 0, SEEK_END) != 0) return {};
    const long end = std::ftell(f);
    if (end < 0) return {};
    if (std::fseek(f, 0, SEEK_SET) != 0) return {};

    std::string out(static_cast<std::size_t>(end), '\0');
    if (!out.empty()) {
        const std::size_t n = std::fread(out.data(), 1, out.size(), f);
        out.resize(n);
    }
    return out;
}

// A sink is a plain function pointer, so the file it writes to is a
// global the two shaped sinks share.
std::FILE* g_sink_out = nullptr;

void text_file_sink(diag::Category cat, std::string_view fn, std::string_view detail) noexcept {
    (void)diag::emit_legacy_text_violation(g_sink_out, cat, fn, detail);
}

void json_file_sink(diag::Category cat, std::string_view fn, std::string_view detail) noexcept {
    (void)diag::emit_json_violation(g_sink_out, cat, fn, detail);
}

// The default sink reads CRUCIBLE_DIAG_FORMAT once per process, so its
// JSON branch cannot be reached from here.  The text branch is what runs
// under ctest; this call exercises it into the process's stderr.
void test_default_sink_emits_text() {
    diag::default_violation_sink(diag::Category::DetSafeLeak, "test_default_sink_emits_text", "expected line");
}

void test_text_shape_through_sink() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr);
    if (f == nullptr) return;
    g_sink_out = f;

    auto previous = diag::set_violation_sink(&text_file_sink);
    diag::report_violation(diag::Category::DetSafeLeak, "fn_name", "detail text");
    diag::set_violation_sink(previous);

    // The prefix is the one external parsers split on.
    EXPECT(read_tmp_file(f) == "crucible-violation: category=DetSafeLeak fn=fn_name detail=detail text\n");
    g_sink_out = nullptr;
    std::fclose(f);
}

// Each text field is capped at 4096 characters.
void test_text_shape_caps_each_field() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr);
    if (f == nullptr) return;

    const std::string long_fn(5000, 'x');
    EXPECT(diag::emit_legacy_text_violation(f, diag::Category::DetSafeLeak, long_fn, "d"));
    const std::string expected =
        "crucible-violation: category=DetSafeLeak fn=" + std::string(4096, 'x') + " detail=d\n";
    EXPECT(read_tmp_file(f) == expected);
    std::fclose(f);

    EXPECT(!diag::emit_legacy_text_violation(nullptr, diag::Category::DetSafeLeak, "fn", "d"));
}

void test_json_shape_through_sink() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr);
    if (f == nullptr) return;
    g_sink_out = f;

    auto previous = diag::set_violation_sink(&json_file_sink);
    diag::report_violation_at(diag::Category::HotPathViolation, "caller row lacks Bg");
    diag::set_violation_sink(previous);

    const std::string json = read_tmp_file(f);
    g_sink_out = nullptr;
    std::fclose(f);

    EXPECT(json.starts_with("{\"format_version\":1,\"source_position\":{\"file\":\""));
    EXPECT(json.find("test_diag.cpp\",\"line\":") != std::string::npos);
    EXPECT(json.find("\"line\":0,") == std::string::npos);
    EXPECT(json.find("\"function\":\"") != std::string::npos);
    EXPECT(json.find("test_json_shape_through_sink") != std::string::npos);
    EXPECT(json.find("\"error_code\":\"HotPathViolation\"") != std::string::npos);
    EXPECT(json.find("\"gap\":\"caller row lacks Bg\"") != std::string::npos);
    EXPECT(json.ends_with("\"related_snippets\":[]}\n"));
}

// The violation record is the composite context taken apart plus the
// catalog's prose for the category, in the fixed field order.
void test_json_violation_record_shape() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr);
    if (f == nullptr) return;

    constexpr diag::Category cat = diag::Category::EffectRowMismatch;
    EXPECT(diag::emit_json_violation(f, cat, "test_diag.cpp:77:9@test_fn", "caller row lacks Bg"));
    const std::string expected = std::string{"{\"format_version\":1,\"source_position\":{\"file\":\"test_diag.cpp\","
                                             "\"line\":77,\"column\":9,\"function\":\"test_fn\"},"
                                             "\"error_code\":\"EffectRowMismatch\",\"goal\":\""}
                               + std::string{diag::description_of(cat)} + "\",\"have\":\"test_fn\","
                               + "\"gap\":\"caller row lacks Bg\",\"suggestion\":\""
                               + std::string{diag::remediation_of(cat)} + "\",\"related_snippets\":[]}\n";
    EXPECT(read_tmp_file(f) == expected);
    std::fclose(f);

    const diag::JsonDiagnosticRecord rec = diag::record_from_violation(cat, "plain_fn", "gap");
    EXPECT(rec.category == cat);
    EXPECT(rec.source.file.empty());
    EXPECT(rec.source.function == "plain_fn");
    EXPECT(rec.have == "plain_fn");
    EXPECT(rec.error_code == diag::name_of(cat));
}

// Every escape the emitter knows, in one record, compared byte for byte.
void test_json_record_escapes() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr);
    if (f == nullptr) return;

    const bool ok = diag::emit_json_record(f, diag::JsonDiagnosticRecord{
                                                  .category = diag::Category::EffectRowMismatch,
                                                  .source =
                                                      diag::SourcePosition{
                                                          .file = "escape.cpp",
                                                          .line = 1,
                                                          .column = 2,
                                                          .function = "escape_fn",
                                                      },
                                                  .error_code = "EffectRowMismatch",
                                                  .goal = "goal",
                                                  .have = "have",
                                                  .gap = "quote \" slash \\ newline\n tab \t ctrl \x01",
                                                  .suggestion = "suggestion",
                                                  .related_snippet = "x < y",
                                              });
    EXPECT(ok);
    // A control character below 0x20 comes out as a backslash-u escape
    // with uppercase hex; the six bytes are spelled out so the source
    // holds no universal-character-name of its own.
    const std::string ctrl_escape{'\\', 'u', '0', '0', '0', '1'};
    const std::string expected =
        std::string{R"json({"format_version":1,"source_position":{"file":"escape.cpp","line":1,"column":2,)json"
                    R"json("function":"escape_fn"},"error_code":"EffectRowMismatch","goal":"goal","have":"have",)json"
                    R"json("gap":"quote \" slash \\ newline\n tab \t ctrl )json"}
        + ctrl_escape + R"json(","suggestion":"suggestion","related_snippets":["x < y"]})json" + "\n";
    EXPECT(read_tmp_file(f) == expected);
    std::fclose(f);

    // Empty code / goal / suggestion fall back to the catalog's prose.
    std::FILE* g = std::tmpfile();
    EXPECT(g != nullptr);
    if (g == nullptr) return;
    EXPECT(diag::emit_json_record(g, diag::JsonDiagnosticRecord{.category = diag::Category::DetSafeLeak}));
    const std::string fallback = read_tmp_file(g);
    std::fclose(g);
    EXPECT(fallback.find("\"error_code\":\"DetSafeLeak\"") != std::string::npos);
    EXPECT(fallback.find("\"goal\":\"" + std::string{diag::DetSafeLeak::description}) != std::string::npos);
    EXPECT(fallback.find("\"suggestion\":\"" + std::string{diag::DetSafeLeak::remediation}) != std::string::npos);
}

// The record is built in a 32 KiB fixed buffer.  A record that does not
// fit is refused whole: nothing reaches the stream.
void test_json_fixed_buffer_bounds() {
    std::FILE* f = std::tmpfile();
    EXPECT(f != nullptr);
    if (f == nullptr) return;

    const std::string too_long(40000, 'g');
    EXPECT(!diag::emit_json_violation(f, diag::Category::BudgetExceeded, "fn", too_long));
    EXPECT(read_tmp_file(f).empty());

    const std::string fits(1000, 'g');
    EXPECT(diag::emit_json_violation(f, diag::Category::BudgetExceeded, "fn", fits));
    EXPECT(read_tmp_file(f).find(fits) != std::string::npos);
    std::fclose(f);

    EXPECT(!diag::emit_json_violation(nullptr, diag::Category::BudgetExceeded, "fn", "d"));

    // The buffer itself: a full buffer refuses one more byte, an
    // over-long append is refused whole, and a failed write poisons the
    // flush.
    diag::detail::fixed_json_buffer<8> buf;
    EXPECT(buf.append("12345678"));
    EXPECT(!buf.push('9'));
    std::FILE* g = std::tmpfile();
    EXPECT(g != nullptr);
    if (g != nullptr) {
        EXPECT(!buf.flush(g));
        std::fclose(g);
    }

    diag::detail::fixed_json_buffer<8> fresh;
    EXPECT(!fresh.append("123456789"));
    EXPECT(!fresh.append("1"));
}

void test_source_position_parser() {
    const auto pos = diag::parse_source_position("/tmp/project/test.cpp:42:7@void ns::fn()");
    EXPECT(pos.file == "/tmp/project/test.cpp");
    EXPECT(pos.line == 42);
    EXPECT(pos.column == 7);
    EXPECT(pos.function == "void ns::fn()");

    const auto legacy = diag::parse_source_position("plain_function");
    EXPECT(legacy.file.empty());
    EXPECT(legacy.function == "plain_function");

    const auto line_only = diag::parse_source_position("only.cpp:12@fn");
    EXPECT(line_only.file == "only.cpp");
    EXPECT(line_only.line == 12);
    EXPECT(line_only.column == 0);

    const auto oversized = diag::parse_source_position("overflow.cpp:999999999999999999999999:7@overflow_fn");
    EXPECT(oversized.file == "overflow.cpp:999999999999999999999999:7");
    EXPECT(oversized.line == 0);

    const auto empty = diag::parse_source_position("");
    EXPECT(empty.file.empty());
    EXPECT(empty.function.empty());
}

}  // namespace

int main() {
    test_runtime_smoke();
    test_catalog_floor();
    test_accessor_runtime_coverage();
    test_categories_array();
    test_enumerate_categories_order();
    test_accessor_strings_match_tag_fields();
    test_insights_runtime();

    test_default_sink_installed();
    test_custom_sink_install_and_route();
    test_null_sink_drops_silently();
    test_all_categories_route_through_sink();
    test_long_input_reaches_sink_once();
    test_empty_strings_handled();
    test_sink_install_round_trips();
    test_report_violation_at_captures_location();
    test_macro_captures_location();
    test_abort_emitters_reach_sink_then_abort();

    test_default_sink_emits_text();
    test_text_shape_through_sink();
    test_text_shape_caps_each_field();
    test_json_shape_through_sink();
    test_json_violation_record_shape();
    test_json_record_escapes();
    test_json_fixed_buffer_bounds();
    test_source_position_parser();

    if (g_failures > 0) {
        std::fprintf(stderr, "FAILURES: %d\n", g_failures);
        return 1;
    }
    return 0;
}
