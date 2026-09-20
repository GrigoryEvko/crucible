#pragma once

// A structured rejection says what failed. The content it prints says
// why the rule exists, how the violation usually reaches a call site,
// and what the compliant line looks like beside the violating one, so
// the reader does not have to go and ask.
//
// This header reads that content off the tag. Each tag in
// foundation/diag/Catalog.h states its own severity and its own four
// prose fields, beside the name and the description it already carries,
// and the authoring rule for them is stated there. This file used to
// hold one explicit insight_provider specialization per tag, which
// spelled the tag name a second time and put the prose in a different
// file from the tag it describes.
//
// A tag that states none of the five reads as empty fields and an Error
// severity. The builder skips an empty field, so such a tag degrades to
// the shorter block rather than emitting empty sections. Stating one
// field is therefore always additive.

#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/reflect/Enumerate.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

namespace foundation::diag {

// Severity is declared in Catalog.h, beside the tags it grades.

// The name is the enumerator's own, read by reflection, so a new
// severity needs no arm here.  A value no enumerator carries, which
// only a cast can produce, comes back as the sentinel.
[[nodiscard]] constexpr std::string_view severity_name(Severity s) noexcept {
    const std::string_view name = ::foundation::reflect::enumerator_name(s);
    return name.empty() ? std::string_view{"<unknown Severity>"} : name;
}

namespace detail {

// A tag states its severity the way it states its name, and a tag that
// states none is an Error.
template <typename Tag>
[[nodiscard]] consteval Severity severity_of_() noexcept {
    if constexpr (requires { Severity{Tag::severity}; }) {
        return Tag::severity;
    } else {
        return Severity::Error;
    }
}

// One reader per prose field.  The requires-clause asks whether the tag
// declares the member and whether it reads as text, so a tag that
// declares none of them, or declares one of an unrelated type, reads as
// empty rather than failing to compile.
#define FOUNDATION_DIAG_INSIGHT_READER(Member)                         \
    template <typename Tag>                                            \
    [[nodiscard]] consteval std::string_view Member##_of_() noexcept { \
        if constexpr (requires { std::string_view{Tag::Member}; }) {   \
            return std::string_view{Tag::Member};                      \
        } else {                                                       \
            return {};                                                 \
        }                                                              \
    }

FOUNDATION_DIAG_INSIGHT_READER(why_this_matters)
FOUNDATION_DIAG_INSIGHT_READER(symptom_pattern)
FOUNDATION_DIAG_INSIGHT_READER(correct_example)
FOUNDATION_DIAG_INSIGHT_READER(violating_example)

#undef FOUNDATION_DIAG_INSIGHT_READER

}  // namespace detail

// The prose lives on the tag, beside the name and the description it
// already carries, so the tag name is spelled once rather than once
// here and once in the catalog.  A tag that declares no prose reads as
// empty fields and Error severity, and the builder skips an empty
// field, so such a tag degrades to the shorter block rather than
// emitting empty sections.
//
// An explicit specialization still beats this primary, which is what
// CRUCIBLE_DEFINE_INSIGHTS below writes.  A tag whose prose cannot sit
// on the tag itself, because the tag belongs to someone else, keeps
// that route.
template <typename Tag>
struct insight_provider {
    static constexpr Severity severity = detail::severity_of_<Tag>();
    static constexpr std::string_view why_this_matters = detail::why_this_matters_of_<Tag>();
    static constexpr std::string_view symptom_pattern = detail::symptom_pattern_of_<Tag>();
    static constexpr std::string_view correct_example = detail::correct_example_of_<Tag>();
    static constexpr std::string_view violating_example = detail::violating_example_of_<Tag>();
};

// Name the tag fully qualified. A specialization has to be declared in
// the namespace of its primary template, so the macro reopens that
// namespace, and an unqualified name written at the call site would be
// looked up there rather than where it was written. Reopening leaves the
// surrounding namespace unchanged, so the macro works at any namespace
// scope.
//
// A second invocation for one tag is a redefinition. Changing a
// severity means removing the first invocation, which puts the change in
// front of a reviewer.
#define CRUCIBLE_DEFINE_INSIGHTS(TagType, Sev, Why, Symptom, Correct, Violating) \
    namespace foundation::diag {                                                 \
    template <>                                                                  \
    struct insight_provider<TagType> {                                           \
        static constexpr Severity severity = (Sev);                              \
        static constexpr std::string_view why_this_matters = (Why);              \
        static constexpr std::string_view symptom_pattern = (Symptom);           \
        static constexpr std::string_view correct_example = (Correct);           \
        static constexpr std::string_view violating_example = (Violating);       \
    };                                                                           \
    }                                                                            \
    static_assert(true, "force trailing semicolon at call site")

// This pins a severity while the prose is still missing. The result is a
// registered tag with nothing to say, which the predicates below still
// report as uninsighted.  It is the full registration with every prose
// field empty.
#define CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(TagType, Sev)                                                      \
    CRUCIBLE_DEFINE_INSIGHTS(TagType, Sev, ::std::string_view{}, ::std::string_view{}, ::std::string_view{}, \
                             ::std::string_view{})

// The same registration with a floor under each field, so a placeholder
// left in one of them fails the build instead of reaching a reader.
// The floors are the ones has_substantive_insights_v reads, stated one
// field at a time so the failing field is named.
#define CRUCIBLE_DEFINE_INSIGHTS_QV(TagType, Sev, Why, Symptom, Correct, Violating)                     \
    CRUCIBLE_DEFINE_INSIGHTS(TagType, Sev, Why, Symptom, Correct, Violating);                           \
    static_assert(::foundation::diag::insight_provider<TagType>::why_this_matters.size()                \
                      >= ::foundation::diag::insights_quality_thresholds<TagType>::min_why_chars,       \
                  "Insight 'why_this_matters' is too short — be substantive. "                          \
                  "Override via insights_quality_thresholds<Tag>::min_why_chars.");                     \
    static_assert(::foundation::diag::insight_provider<TagType>::symptom_pattern.size()                 \
                      >= ::foundation::diag::insights_quality_thresholds<TagType>::min_symptom_chars,   \
                  "Insight 'symptom_pattern' is too short — be substantive. "                           \
                  "Override via insights_quality_thresholds<Tag>::min_symptom_chars.");                 \
    static_assert(::foundation::diag::insight_provider<TagType>::correct_example.size()                 \
                      >= ::foundation::diag::insights_quality_thresholds<TagType>::min_correct_chars,   \
                  "Insight 'correct_example' is too short — show real C++. "                            \
                  "Override via insights_quality_thresholds<Tag>::min_correct_chars.");                 \
    static_assert(::foundation::diag::insight_provider<TagType>::violating_example.size()               \
                      >= ::foundation::diag::insights_quality_thresholds<TagType>::min_violating_chars, \
                  "Insight 'violating_example' is too short — show the anti-pattern. "                  \
                  "Override via insights_quality_thresholds<Tag>::min_violating_chars.")

// Specialize this per tag for a stricter or looser bar.
template <typename Tag>
struct insights_quality_thresholds {
    static constexpr std::size_t min_why_chars = 30;
    static constexpr std::size_t min_symptom_chars = 20;
    static constexpr std::size_t min_correct_chars = 10;
    static constexpr std::size_t min_violating_chars = 10;
};

// A specialization written elsewhere works the same way. It sits at
// namespace scope beside the tag it describes and does not enter the
// closed catalog of tags this header knows about.

// This fence is the bottom of the effect lattice, an empty row that
// rejects every atom. The general row mismatch is a different thing: a
// row one caller happens to impose.
// One step up the lattice: a loop that may not terminate is admitted,
// allocation and input or output are not. Reaching for those means
// lifting one step further.
// One step further: state and divergence are admitted, the
// context-bound capabilities are not. Those name a dispatch position
// rather than an effect, and only the top of the lattice admits them.
// One non-empty field is enough. This is what the builder reads to
// decide whether to emit insight sections at all.

template <typename Tag>
inline constexpr bool has_insights_v =
    !insight_provider<Tag>::why_this_matters.empty() || !insight_provider<Tag>::symptom_pattern.empty()
    || !insight_provider<Tag>::correct_example.empty() || !insight_provider<Tag>::violating_example.empty();

// Every field, against its own threshold. The strictly stronger
// predicate of the two.

template <typename Tag>
inline constexpr bool has_substantive_insights_v =
    insight_provider<Tag>::why_this_matters.size() >= insights_quality_thresholds<Tag>::min_why_chars
    && insight_provider<Tag>::symptom_pattern.size() >= insights_quality_thresholds<Tag>::min_symptom_chars
    && insight_provider<Tag>::correct_example.size() >= insights_quality_thresholds<Tag>::min_correct_chars
    && insight_provider<Tag>::violating_example.size() >= insights_quality_thresholds<Tag>::min_violating_chars;

template <typename Tag>
concept WellInsightedTag = is_diagnostic_class_v<Tag> && has_insights_v<Tag>;

template <typename Tag>
concept HasSubstantiveInsights = is_diagnostic_class_v<Tag> && has_substantive_insights_v<Tag>;

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
    return (HasSubstantiveInsights<std::tuple_element_t<Is, Catalog>> && ...);
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
                                                    "CRUCIBLE_DEFINE_INSIGHTS expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

}  // namespace foundation::diag::detail::insights_macro_test

// Invoking the macro at namespace scope is the shape a consumer uses.
CRUCIBLE_DEFINE_INSIGHTS(::foundation::diag::detail::insights_macro_test::macro_target_tag,
                         ::foundation::diag::Severity::Warning, "WHY-MACRO-TEST", "SYMPTOM-MACRO-TEST",
                         "CORRECT-MACRO-TEST", "VIOLATING-MACRO-TEST");

namespace foundation::diag::detail::insights_macro_test {

using P = ::foundation::diag::insight_provider<macro_target_tag>;

static_assert(P::severity == ::foundation::diag::Severity::Warning,
              "CRUCIBLE_DEFINE_INSIGHTS failed to set severity correctly.");
static_assert(P::why_this_matters == std::string_view{"WHY-MACRO-TEST"});
static_assert(P::symptom_pattern == std::string_view{"SYMPTOM-MACRO-TEST"});
static_assert(P::correct_example == std::string_view{"CORRECT-MACRO-TEST"});
static_assert(P::violating_example == std::string_view{"VIOLATING-MACRO-TEST"});
static_assert(::foundation::diag::has_insights_v<macro_target_tag>,
              "Macro-populated insights must register as has_insights_v.");

struct severity_only_tag : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "SeverityOnlyTag";
    static constexpr std::string_view description = "test fixture for CRUCIBLE_DEFINE_INSIGHTS_SEVERITY expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

struct qv_target_tag : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "QvTargetTag";
    static constexpr std::string_view description = "test fixture for CRUCIBLE_DEFINE_INSIGHTS_QV expansion";
    static constexpr std::string_view remediation = "n/a — fixture";
};

}  // namespace foundation::diag::detail::insights_macro_test

CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(::foundation::diag::detail::insights_macro_test::severity_only_tag,
                                  ::foundation::diag::Severity::Fatal);

// Each field here clears its default threshold.
CRUCIBLE_DEFINE_INSIGHTS_QV(::foundation::diag::detail::insights_macro_test::qv_target_tag,
                            ::foundation::diag::Severity::Error,
                            "QV why field — substantive prose clearing the 30-char min.",
                            "QV symptom — clears 20-char min.",
                            "fn(GoodFoo);",  // 12 chars — clears 10-char min
                            "fn(BadBar);   // VIOLATES the contract");

namespace foundation::diag::detail::insights_macro_test {

using PSev = ::foundation::diag::insight_provider<severity_only_tag>;
static_assert(PSev::severity == ::foundation::diag::Severity::Fatal,
              "CRUCIBLE_DEFINE_INSIGHTS_SEVERITY must set severity.");
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
