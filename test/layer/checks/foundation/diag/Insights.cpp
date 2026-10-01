// The compile-time checks of foundation/diag/Insights.h.

#include <foundation/diag/Insights.h>

namespace foundation::diag {

namespace detail::insights_self_test {

// Every enumerator has a name, and the name is its own; the sentinel is
// reserved for a value that no enumerator carries.
[[nodiscard]] consteval bool every_severity_has_its_name() noexcept {
    bool all_named = true;
    ::foundation::reflect::for_each_enumerator<Severity>(
        [&](Severity value, std::string_view name) noexcept { all_named = all_named && severity_name(value) == name; });
    return all_named;
}
static_assert(every_severity_has_its_name());
static_assert(severity_name(Severity::Fatal) == "Fatal");
static_assert(severity_name(static_cast<Severity>(9)) == "<unknown Severity>");

static_assert(insight_provider<EpochMismatch>::severity == Severity::Error,
              "an unspecialized tag must default to Error severity");

// Every tag in the catalog is insighted, and substantively so: the
// walk is over the catalog tuple, so a tag appended without prose
// fails here rather than joining a hand list nobody extends.
template <std::size_t... Is>
[[nodiscard]] consteval bool every_catalog_tag_has_substantive_insights(std::index_sequence<Is...>) noexcept {
    return (HasSubstantiveInsights<std::tuple_element_t<Is, Catalog<>>> && ...);
}
static_assert(every_catalog_tag_has_substantive_insights(std::make_index_sequence<catalog_size>{}),
              "a catalog tag has no insight_provider specialization, or one of its prose fields is "
              "below the insights_quality_thresholds floor");

static_assert(insight_provider<DetSafeLeak>::severity == Severity::Fatal);

static_assert(insight_provider<UnknownParameterShape>::severity == Severity::Warning);

static_assert(insight_provider<HotPathViolation>::severity == Severity::Error);
static_assert(insight_provider<EffectRowMismatch>::severity == Severity::Error);

struct user_tag : tag_base {
    static constexpr std::string_view name = "UserDefinedX";
    static constexpr std::string_view description = "user";
    static constexpr std::string_view remediation = "see local docs";
};
static_assert(!has_insights_v<user_tag>);
static_assert(insight_provider<user_tag>::severity == Severity::Error);

// The member route, on a tag the catalog does not know.  Each field is
// read off the tag by name.
struct carrying_tag : tag_base {
    static constexpr std::string_view name = "CarryingTag";
    static constexpr std::string_view description = "fixture for the member route";
    static constexpr std::string_view remediation = "n/a — fixture";

    static constexpr Severity severity = Severity::Warning;
    static constexpr std::string_view why_this_matters = "the why field, carried on the tag";
    static constexpr std::string_view symptom_pattern = "the symptom field";
    static constexpr std::string_view correct_example = "fn(Good);";
    static constexpr std::string_view violating_example = "fn(Bad);";
};

static_assert(insight_provider<carrying_tag>::severity == Severity::Warning);
static_assert(insight_provider<carrying_tag>::why_this_matters == "the why field, carried on the tag");
static_assert(insight_provider<carrying_tag>::symptom_pattern == "the symptom field");
static_assert(insight_provider<carrying_tag>::correct_example == "fn(Good);");
static_assert(insight_provider<carrying_tag>::violating_example == "fn(Bad);");
static_assert(has_insights_v<carrying_tag>);

// One field on its own reads back, and the other three stay empty.  A
// tag that carries some prose and not the rest is the state a partly
// written entry is in, and the reader does not confuse the fields.
struct partly_carrying_tag : tag_base {
    static constexpr std::string_view name = "PartlyCarryingTag";
    static constexpr std::string_view description = "fixture for a single carried field";
    static constexpr std::string_view remediation = "n/a — fixture";

    static constexpr std::string_view correct_example = "only_this_one();";
};

static_assert(insight_provider<partly_carrying_tag>::correct_example == "only_this_one();");
static_assert(insight_provider<partly_carrying_tag>::why_this_matters.empty());
static_assert(insight_provider<partly_carrying_tag>::symptom_pattern.empty());
static_assert(insight_provider<partly_carrying_tag>::violating_example.empty());
static_assert(insight_provider<partly_carrying_tag>::severity == Severity::Error,
              "a tag declaring no severity reads as Error");
static_assert(has_insights_v<partly_carrying_tag>);
static_assert(!has_substantive_insights_v<partly_carrying_tag>);

// The catalog walk above covers every admitting case of both concepts.
// The rejection cases use the tag above, which carries the empty
// defaults, and a type that is not a tag at all.
static_assert(!WellInsightedTag<user_tag>);
static_assert(!WellInsightedTag<int>);
static_assert(!HasSubstantiveInsights<user_tag>);

}  // namespace detail::insights_self_test

}  // namespace foundation::diag

namespace foundation::diag::detail::insights_macro_test {

struct macro_target_tag : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "MacroTargetTag";
    static constexpr std::string_view description = "test fixture for "
                                                    "CRUCIBLE_DIAG_INSIGHTS expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

}  // namespace foundation::diag::detail::insights_macro_test

// Invoking the macro at namespace scope is the shape a consumer uses.
CRUCIBLE_DIAG_INSIGHTS(::foundation::diag::detail::insights_macro_test::macro_target_tag,
                       ::foundation::diag::Severity::Warning, "WHY-MACRO-TEST", "SYMPTOM-MACRO-TEST",
                       "CORRECT-MACRO-TEST", "VIOLATING-MACRO-TEST");

namespace foundation::diag::detail::insights_macro_test {

using P = ::foundation::diag::insight_provider<macro_target_tag>;

static_assert(P::severity == ::foundation::diag::Severity::Warning,
              "CRUCIBLE_DIAG_INSIGHTS failed to set severity correctly.");
static_assert(P::why_this_matters == std::string_view{"WHY-MACRO-TEST"});
static_assert(P::symptom_pattern == std::string_view{"SYMPTOM-MACRO-TEST"});
static_assert(P::correct_example == std::string_view{"CORRECT-MACRO-TEST"});
static_assert(P::violating_example == std::string_view{"VIOLATING-MACRO-TEST"});
static_assert(::foundation::diag::has_insights_v<macro_target_tag>,
              "Macro-populated insights must register as has_insights_v.");

struct severity_only_tag : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "SeverityOnlyTag";
    static constexpr std::string_view description = "test fixture for CRUCIBLE_DIAG_INSIGHTS_SEVERITY expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

struct qv_target_tag : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "QvTargetTag";
    static constexpr std::string_view description = "test fixture for CRUCIBLE_DIAG_INSIGHTS_QV expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

}  // namespace foundation::diag::detail::insights_macro_test

CRUCIBLE_DIAG_INSIGHTS_SEVERITY(::foundation::diag::detail::insights_macro_test::severity_only_tag,
                                ::foundation::diag::Severity::Fatal);

// Each field here clears its default threshold.
CRUCIBLE_DIAG_INSIGHTS_QV(::foundation::diag::detail::insights_macro_test::qv_target_tag,
                          ::foundation::diag::Severity::Error,
                          "QV why field — substantive prose clearing the 30-char min.",
                          "QV symptom — clears 20-char min.",
                          "fn(GoodFoo);",  // 12 chars — clears 10-char min
                          "fn(BadBar);   // VIOLATES the contract");

namespace foundation::diag::detail::insights_macro_test {

using PSev = ::foundation::diag::insight_provider<severity_only_tag>;
static_assert(PSev::severity == ::foundation::diag::Severity::Fatal,
              "CRUCIBLE_DIAG_INSIGHTS_SEVERITY must set severity.");
static_assert(PSev::why_this_matters.empty(), "Severity-only macro should leave why_this_matters empty.");
static_assert(PSev::symptom_pattern.empty(), "Severity-only macro should leave symptom_pattern empty.");
// Registering a severity alone leaves every prose field empty, and the
// predicate reports that state as uninsighted rather than registered.
static_assert(!::foundation::diag::has_insights_v<severity_only_tag>,
              "Severity-only macro leaves has_insights_v false (prose all empty).");

using PQv = ::foundation::diag::insight_provider<qv_target_tag>;
static_assert(PQv::severity == ::foundation::diag::Severity::Error);
static_assert(PQv::why_this_matters.size() >= 30);
static_assert(PQv::symptom_pattern.size() >= 20);
static_assert(PQv::correct_example.size() >= 10);
static_assert(PQv::violating_example.size() >= 10);
static_assert(::foundation::diag::has_insights_v<qv_target_tag>);
static_assert(::foundation::diag::has_substantive_insights_v<qv_target_tag>);
static_assert(::foundation::diag::WellInsightedTag<qv_target_tag>);
static_assert(::foundation::diag::HasSubstantiveInsights<qv_target_tag>);

}  // namespace foundation::diag::detail::insights_macro_test
