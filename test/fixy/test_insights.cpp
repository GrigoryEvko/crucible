// Sentinel TU for fixy/Insights.h: every axis has a provider on its
// duplicate tag, the provider's text names the axis and the strict pole
// that Axis.h declares, no generated text names a pole the table does
// not have, the six corpus entries are insighted at Fatal, and the
// strings exist at runtime.
//
// The pole name is spliced here a second time, from the table and by
// reflection, so the check is against Axis.h and not against the
// header's own helper.  A provider whose text named a default the
// table does not carry would fail here whatever the header's helper
// says.

#include <fixy/Insights.h>

#include <fixy/Axis.h>
#include <fixy/Corpus.h>
#include <fixy/Reject.h>
#include <foundation/diag/Insights.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

namespace fd = ::foundation::diag;
using ::fixy::Axis;
using ::fixy::axis_name;
using ::fixy::axis_traits;
using ::fixy::duplicate_atom_on;

// ---------------------------------------------------------------------
// The independent splice.  Three shapes: a value pole named by its
// value, a specialisation named by its template, a class named by
// itself; and the caller-supplied axis, which has no pole.

// A text search for constant expressions.  string_view::find
// null-checks a pointer into the define_static_string object behind
// every generated text, which GCC 16.2's constant evaluator refuses;
// indexing is accepted.
[[nodiscard]] consteval bool contains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.size() > haystack.size()) return false;
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool same = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            if (haystack[start + offset] != needle[offset]) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

[[nodiscard]] consteval std::string_view digits(unsigned long long value) noexcept {
    std::string text;
    do {
        text.insert(text.begin(), static_cast<char>('0' + static_cast<int>(value % 10)));
        value /= 10;
    } while (value != 0);
    return std::define_static_string(text);
}

template <Axis A>
[[nodiscard]] consteval std::string_view expected_pole_name() noexcept {
    if constexpr (::fixy::IsCallerSupplied<A>) {
        return "the payload type the binding names";
    } else {
        using Strict = typename axis_traits<A>::strict;
        if constexpr (requires { Strict::value; }) {
            constexpr auto value = Strict::value;
            if constexpr (std::is_enum_v<decltype(value)>) {
                return ::foundation::reflect::enum_name(value);
            } else {
                return digits(static_cast<unsigned long long>(value));
            }
        } else if constexpr (std::meta::has_template_arguments(std::meta::dealias(^^Strict))) {
            return std::meta::identifier_of(std::meta::template_of(std::meta::dealias(^^Strict)));
        } else {
            return std::meta::identifier_of(std::meta::dealias(^^Strict));
        }
    }
}

// The splice pinned on one pole of each shape, so the walk below is
// not vacuous.
static_assert(expected_pole_name<Axis::Usage>() == "One");
static_assert(expected_pole_name<Axis::Security>() == "Secret");
static_assert(expected_pole_name<Axis::Overflow>() == "Trap");
static_assert(expected_pole_name<Axis::Version>() == "1");
static_assert(expected_pole_name<Axis::Refinement>() == "True");
static_assert(expected_pole_name<Axis::Lifetime>() == "Static");
static_assert(expected_pole_name<Axis::Trust>() == "Unverified");
static_assert(expected_pole_name<Axis::Effect>() == "Row");
static_assert(expected_pole_name<Axis::Observability>() == "Row");
static_assert(expected_pole_name<Axis::Synchronization>() == "Unconstrained");
static_assert(expected_pole_name<Axis::Regime>() == "Unconstrained");
static_assert(expected_pole_name<Axis::Type>() == "the payload type the binding names");

// ---------------------------------------------------------------------
// Every axis has a provider that names the axis and its pole.  The walk
// is over the enum, so an axis added to it is covered the day it is
// declared, and the count of providers is the count of axes.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] consteval std::size_t axes_with_a_provider_naming_their_pole() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        using Tag = duplicate_atom_on<axis>;
        using Provider = fd::insight_provider<Tag>;
        const bool provided = fd::has_insights_v<Tag> && fd::has_substantive_insights_v<Tag>
                           && fd::HasSubstantiveInsights<Tag> && fd::WellInsightedTag<Tag>
                           && Provider::severity == fd::Severity::Error
                           && contains(Provider::why_this_matters, axis_name(axis))
                           && contains(Provider::why_this_matters, expected_pole_name<axis>())
                           && contains(Provider::symptom_pattern, axis_name(axis))
                           && contains(Provider::correct_example, expected_pole_name<axis>())
                           && Provider::correct_example.starts_with("fixy::fn<")
                           && contains(Provider::violating_example, axis_name(axis))
                           && Provider::violating_example.starts_with("fixy::fn<");
        if (provided) ++counted;
    }
    return counted;
}
#pragma GCC diagnostic pop

static_assert(axes_with_a_provider_naming_their_pole() == ::fixy::axis_count,
              "an axis has no substantive provider on its duplicate tag, or the provider's text does not "
              "name the axis and the strict pole Axis.h declares");

// Two axes' texts differ where the table differs.
static_assert(fd::insight_provider<duplicate_atom_on<Axis::Usage>>::why_this_matters
              != fd::insight_provider<duplicate_atom_on<Axis::Effect>>::why_this_matters);
static_assert(fd::insight_provider<duplicate_atom_on<Axis::Usage>>::correct_example
              != fd::insight_provider<duplicate_atom_on<Axis::Security>>::correct_example);

// A tag this header does not describe reads as uninsighted, so the
// providers above are the partial specialisation and not a primary
// that answers for everything.
struct undescribed_tag : fd::tag_base {
    static constexpr std::string_view name = "Undescribed";
    static constexpr std::string_view description = "fixture";
    static constexpr std::string_view remediation = "fixture";
};
static_assert(!fd::has_insights_v<undescribed_tag>);

// ---------------------------------------------------------------------
// The six corpus entries are insighted at Fatal, and their examples
// cite policies that exist.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] consteval std::size_t corpus_entries_insighted_at_fatal() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto entry : std::define_static_array(
                      std::meta::template_arguments_of(std::meta::dealias(^^::fixy::corpus::Entries)))) {
        using Entry = [:entry:];
        using Provider = fd::insight_provider<Entry>;
        const bool provided = fd::HasSubstantiveInsights<Entry> && Provider::severity == fd::Severity::Fatal
                           && Provider::correct_example.starts_with("fixy::fn<")
                           && Provider::violating_example.starts_with("fixy::fn<");
        if (provided) ++counted;
    }
    return counted;
}
#pragma GCC diagnostic pop

static_assert(corpus_entries_insighted_at_fatal() == ::fixy::corpus::corpus_size);

// The entries whose why field is the citation say what the tag says.
static_assert(fd::insight_provider<::fixy::corpus::staleness_secret_without_declassify>::why_this_matters
              == ::fixy::corpus::staleness_secret_without_declassify::cite());
static_assert(fd::insight_provider<::fixy::corpus::ghost_runtime_observable>::why_this_matters
              == ::fixy::corpus::ghost_runtime_observable::cite());
static_assert(fd::insight_provider<::fixy::corpus::internal_bg_without_declassify>::why_this_matters
              == ::fixy::corpus::internal_bg_without_declassify::cite());

// The policies the examples name are declassification policies.
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::WireSerialize>);
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::AuthorizedReplay>);
static_assert(contains(fd::insight_provider<::fixy::corpus::classified_io_without_declassify>::correct_example,
                       "WireSerialize"));
static_assert(contains(fd::insight_provider<::fixy::corpus::staleness_secret_without_declassify>::correct_example,
                       "AuthorizedReplay"));

// ---------------------------------------------------------------------
// A static_assert proves the constant-evaluated path only.  These run.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] int check_runtime_paths() {
    std::size_t walked = 0;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        using Provider = fd::insight_provider<duplicate_atom_on<axis>>;
        const std::string_view why = Provider::why_this_matters;
        const std::string_view symptom = Provider::symptom_pattern;
        const std::string_view correct = Provider::correct_example;
        const std::string_view violating = Provider::violating_example;
        if (why.empty() || symptom.empty() || correct.empty() || violating.empty()) return 1;
        if (!why.contains(axis_name(axis))) return 2;
        ++walked;
    }
    if (walked != ::fixy::axis_count) return 3;

    const std::string_view fatal_why =
        fd::insight_provider<::fixy::corpus::classified_io_without_declassify>::why_this_matters;
    if (fatal_why.empty()) return 4;
    if (fd::severity_name(fd::insight_provider<::fixy::corpus::classified_io_without_declassify>::severity)
        != "Fatal") {
        return 5;
    }
    return 0;
}
#pragma GCC diagnostic pop

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}
