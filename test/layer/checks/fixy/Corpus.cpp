// The compile-time checks of fixy/Corpus.h.

#include <fixy/Corpus.h>

namespace fixy::corpus {

static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::AuthorizedReplay) == DischargeAxis::Staleness,
              "AuthorizedReplay must discharge Staleness and nothing else.  It is the only policy that licenses "
              "the replay window, so a change here removes the sole discharge path for the staleness reject.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::AuditedLogging) == DischargeAxis::IO,
              "AuditedLogging licenses the log line it writes, which goes out through IO, and nothing else.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::WireSerialize) == DischargeAxis::IO,
              "WireSerialize licenses the serialized frame on the wire, which goes out through IO, and nothing "
              "else.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::UserDisplay) == DischargeAxis::IO,
              "UserDisplay licenses the rendered display, which goes out through IO, and nothing else.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::HashForCompare) == DischargeAxis::None,
              "HashForCompare releases a hash of the value and names no channel.  A binding that sends the hash "
              "out needs an export policy.");
static_assert(discharge_mask_of(^^::fixy::tags::secret_policy::LengthOnly) == DischargeAxis::None,
              "LengthOnly releases only size metadata and names no channel.  Size is an information channel, but "
              "no axis in DischargeAxis names it.");
static_assert(discharge_mask_of(^^int) == DischargeAxis::None, "a type that is not a policy licenses nothing");

static_assert(detail::non_none_policy_count_() == 4,
              "Exactly four secret_policy tags license a channel: AuthorizedReplay for Staleness, and "
              "AuditedLogging, WireSerialize and UserDisplay for IO.  A policy that licenses a channel needs a "
              "line in discharge_mask_of, a pin that names the channel, and a change of this count.");

namespace detail {

static_assert(!is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Init>>::value,
              "Init must stay outside the observable set.  Moving it in rejects every ghost binding that "
              "participates in initialization, so it needs its own corpus entry naming the contradiction it "
              "catches.");
static_assert(!is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Test>>::value,
              "Test must stay outside the observable set.  A specification evaluated under a test harness is "
              "legitimate ghost code.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Alloc>>::value,
              "Alloc must stay inside the observable set.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::IO>>::value,
              "IO must stay inside the observable set.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Block>>::value,
              "Block must stay inside the observable set.");
static_assert(is_row_observable_<::foundation::effects::Row<::foundation::effects::Effect::Bg>>::value,
              "Bg must stay inside the observable set.");

}  // namespace detail

namespace detail {
struct corpus_header_site_ final {};
}  // namespace detail

static_assert(corpus_entries_declared_but_not_joined<detail::corpus_header_site_>().empty(),
              corpus_join_diagnostic<detail::corpus_header_site_>());

namespace detail::corpus_self_test {

// Every entry is a diagnostic tag whose name is its own class name, so
// the string the message carries cannot drift from the type the
// instantiation trail names.  Each carries a citation, and its full
// diagnostic names it.
template <class Entry>
[[nodiscard]] consteval bool entry_is_well_formed_() noexcept {
    return ::foundation::diag::is_diagnostic_class_v<Entry> && Entry::name == std::meta::identifier_of(^^Entry)
        && !Entry::description.empty() && !Entry::remediation.empty() && !Entry::cite().empty()
        && Entry::full_diagnostic().starts_with("fixy::fn<") && text_contains(Entry::full_diagnostic(), Entry::name)
        && text_contains(Entry::full_diagnostic(), Entry::cite());
}

[[nodiscard]] consteval bool every_entry_is_well_formed_() noexcept {
    bool all_well_formed = true;
    static constexpr auto entries =
        std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^Entries)));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : entries) {
        using Entry = [:entry:];
        all_well_formed = all_well_formed && entry_is_well_formed_<Entry>();
    }
#pragma GCC diagnostic pop
    return all_well_formed;
}

static_assert(every_entry_is_well_formed_(),
              "fixy/Corpus.h: a corpus entry is not a diagnostic tag, or its name is not its class name, or "
              "one of its texts is empty, or its full diagnostic does not carry its name and its citation.");

// The shortest binding is not in the corpus: every axis at its strict
// pole names no effect and no replay window.
static_assert(!is_in_corpus_v<int>);
static_assert(std::is_same_v<matched_entry_or_void_t<int>, void>);

// Reject by default on the Security axis.  An IO row with no Security
// atom is a classified value on an observable channel, and is refused;
// the same pack naming the public grade is admitted.
static_assert(is_in_corpus_v<int, ::fixy::atom::with_io>);
static_assert(std::is_same_v<matched_entry_or_void_t<int, ::fixy::atom::with_io>, classified_io_without_declassify>);
static_assert(!is_in_corpus_v<int, ::fixy::atom::with_io, ::fixy::atom::as_public>);

// A declassification licenses the channels its policy names, and no
// other.  The wire policy licenses IO.  The replay policy does not, so
// the same IO row under it is classified IO.  No policy licenses Bg.
static_assert(
    !is_in_corpus_v<int, ::fixy::atom::with_io, ::fixy::atom::declassify<::fixy::tags::secret_policy::WireSerialize>>);
static_assert(
    std::is_same_v<matched_entry_or_void_t<int, ::fixy::atom::with_io,
                                           ::fixy::atom::declassify<::fixy::tags::secret_policy::AuthorizedReplay>>,
                   classified_io_without_declassify>);
static_assert(
    std::is_same_v<matched_entry_or_void_t<int, ::fixy::atom::with_bg,
                                           ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>>,
                   classified_bg_without_declassify>);

// An export policy leaves the value a secret where replay is concerned,
// and only the replay policy discharges the axis.
static_assert(is_in_corpus_v<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>,
                             ::fixy::atom::stale_to<5>>);
static_assert(!is_in_corpus_v<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuthorizedReplay>,
                              ::fixy::atom::stale_to<5>>);

// The name and the diagnostic read off a matched pack, and are empty
// off an admitted one.
static_assert(corpus_entry_name_for_v<int, ::fixy::atom::with_io> == "classified_io_without_declassify");
static_assert(text_contains(corpus_full_diagnostic_v<int, ::fixy::atom::with_io>, "Sabelfeld-Myers 2003"));
static_assert(corpus_entry_name_for_v<int>.empty());
static_assert(corpus_full_diagnostic_v<int>.empty());

}  // namespace detail::corpus_self_test

}  // namespace fixy::corpus
