// The compile-time checks of fixy/Insights.h.

#include <fixy/Insights.h>

// The four policies below are named verbatim inside the inert strings of
// fixy/Insights.h, which no compiler ever checks.  These pins put a
// rename or a removal in front of the reader who has to update those
// strings.  They live in the check file of the citing header rather than
// beside the policy definitions, because the citation is what breaks.
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::WireSerialize>,
              "tags::secret_policy::WireSerialize must exist and be a declassification policy: it is cited "
              "verbatim in the correct_example of classified_io_without_declassify above.");
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::AuthorizedReplay>,
              "tags::secret_policy::AuthorizedReplay must exist and be a declassification policy: it is cited "
              "verbatim in the correct_example of staleness_secret_without_declassify above, and it is the "
              "one policy that discharges Staleness.");
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::AuditedLogging>,
              "tags::secret_policy::AuditedLogging must exist and be a declassification policy: the roles and "
              "the corpus self-tests spell it, and the why_this_matters of classified_io_without_declassify "
              "above names it.");
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::UserDisplay>,
              "tags::secret_policy::UserDisplay must exist and be a declassification policy: the "
              "why_this_matters of classified_io_without_declassify above names it.");

namespace fixy::insights::detail::insights_self_test {

// Every axis has a provider, it clears the substance floor, and its
// text names the axis and the strict pole the table declares.
[[nodiscard]] consteval bool every_axis_has_a_provider_() noexcept {
    bool all_provided = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        using Tag = ::fixy::duplicate_atom_on<axis>;
        using Provider = ::foundation::diag::insight_provider<Tag>;
        all_provided = all_provided && ::foundation::diag::HasSubstantiveInsights<Tag>
                    && ::fixy::detail::text_contains(Provider::why_this_matters, ::fixy::axis_name(axis))
                    && ::fixy::detail::text_contains(Provider::why_this_matters, strict_pole_name_<axis>())
                    && ::fixy::detail::text_contains(Provider::violating_example, ::fixy::axis_name(axis));
    }
#pragma GCC diagnostic pop
    return all_provided;
}

static_assert(every_axis_has_a_provider_(),
              "fixy/Insights.h: an axis has no substantive insight provider on its duplicate tag, or the "
              "provider's text does not name the axis and its strict pole.");

// The pole names, one per shape the table carries: an enum value, an
// integer value, a plain class, a specialisation, and the caller-
// supplied axis.
static_assert(strict_pole_name_<Axis::Usage>() == "One");
static_assert(strict_pole_name_<Axis::Security>() == "Secret");
static_assert(strict_pole_name_<Axis::Version>() == "Unconstrained");
static_assert(strict_pole_name_<Axis::Refinement>() == "True");
static_assert(strict_pole_name_<Axis::Effect>() == "Row");
static_assert(strict_pole_name_<Axis::Regime>() == "Unconstrained");
static_assert(strict_pole_name_<Axis::Type>() == "the payload type the binding names");

// The six corpus entries are insighted, at Fatal, and each text of each
// entry clears the floor of insights_quality_thresholds, the floor that
// CRUCIBLE_DIAG_INSIGHTS_QV states.  The fault names the first entry and
// field that fail, and it is empty when each one holds.
[[nodiscard]] consteval std::string_view corpus_insight_fault_() {
    static constexpr auto entries =
        std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^::fixy::corpus::Entries)));
    std::string fault;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : entries) {
        using Entry = [:entry:];
        using Provider = ::foundation::diag::insight_provider<Entry>;
        using Floor = ::foundation::diag::insights_quality_thresholds<Entry>;
        const std::string name{std::meta::display_string_of(entry)};
        if (!fault.empty()) {
            continue;
        }
        if (!::foundation::diag::is_diagnostic_class_v<Entry>) {
            fault = name + " is not a diagnostic class of the catalog.";
        } else if (Provider::severity != ::foundation::diag::Severity::Fatal) {
            fault = name + " has an insight severity that is not Fatal.";
        } else if (Provider::why_this_matters.size() < Floor::min_why_chars) {
            fault = name + ": the insight why_this_matters is too short.  Be substantive.";
        } else if (Provider::symptom_pattern.size() < Floor::min_symptom_chars) {
            fault = name + ": the insight symptom_pattern is too short.  Be substantive.";
        } else if (Provider::correct_example.size() < Floor::min_correct_chars) {
            fault = name + ": the insight correct_example is too short.  Show real C++.";
        } else if (Provider::violating_example.size() < Floor::min_violating_chars) {
            fault = name + ": the insight violating_example is too short.  Show the anti-pattern.";
        }
    }
#pragma GCC diagnostic pop
    return std::define_static_string(fault);
}

static_assert(corpus_insight_fault_().empty(), corpus_insight_fault_());

}  // namespace fixy::insights::detail::insights_self_test
