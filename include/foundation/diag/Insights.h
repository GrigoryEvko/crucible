#pragma once

// A structured rejection says what failed. The content it prints says
// why the rule exists, how the violation usually reaches a call site,
// and what the compliant line looks like beside the violating one, so
// the reader does not have to go and ask.
//
// This header reads that content off the tag. Each tag in
// foundation/diag/Catalog.h states its own severity and its own four
// prose fields, beside the name and the description it already carries,
// and the authoring rule for them is stated there. So the prose stays
// in the file of the tag that it describes.
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
// CRUCIBLE_DIAG_INSIGHTS below writes.  A tag whose prose cannot sit
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
#define CRUCIBLE_DIAG_INSIGHTS(TagType, Sev, Why, Symptom, Correct, Violating) \
    namespace foundation::diag {                                               \
    template <>                                                                \
    struct insight_provider<TagType> {                                         \
        static constexpr Severity severity = (Sev);                            \
        static constexpr std::string_view why_this_matters = (Why);            \
        static constexpr std::string_view symptom_pattern = (Symptom);         \
        static constexpr std::string_view correct_example = (Correct);         \
        static constexpr std::string_view violating_example = (Violating);     \
    };                                                                         \
    }                                                                          \
    static_assert(true, "force trailing semicolon at call site")

// This pins a severity while the prose is still missing. The result is a
// registered tag with nothing to say, which the predicates below still
// report as uninsighted.  It is the full registration with every prose
// field empty.
#define CRUCIBLE_DIAG_INSIGHTS_SEVERITY(TagType, Sev)                                                      \
    CRUCIBLE_DIAG_INSIGHTS(TagType, Sev, ::std::string_view{}, ::std::string_view{}, ::std::string_view{}, \
                           ::std::string_view{})

// The same registration with a floor under each field, so a placeholder
// left in one of them fails the build instead of reaching a reader.
// The floors are the ones has_substantive_insights_v reads, stated one
// field at a time so the failing field is named.
#define CRUCIBLE_DIAG_INSIGHTS_QV(TagType, Sev, Why, Symptom, Correct, Violating)                       \
    CRUCIBLE_DIAG_INSIGHTS(TagType, Sev, Why, Symptom, Correct, Violating);                             \
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

}  // namespace foundation::diag
