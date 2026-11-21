// The compile-time checks of fixy/Corpus.h.

#include <fixy/Corpus.h>

#include <foundation/core/Algorithms.h>
#include <foundation/reflect/ReflectionList.h>

namespace fixy::corpus {

static_assert(detail::non_none_policy_count_() == 4,
              "Exactly four secret_policy tags license a channel: AuthorizedReplay for Staleness, and "
              "AuditedLogging, WireSerialize and UserDisplay for IO.  A policy that licenses a channel needs a "
              "line in discharge_mask_of, a pin that names the channel, and a change of this count.");

// Each matcher reads the premises of the binding.  The ghost entry reads each
// observable effect of the row through the premises row_observable and
// row_bg, and Effect.h decides which effects count.  A stated with<IO>
// answers as the row Row<IO> does.
namespace premise_cells {
using ::fixy::collision::pack_verdict_v;
namespace at = ::fixy::atom;
using Effect = ::foundation::effects::Effect;

template <Effect... Es>
inline constexpr bool ghost_with_row_matches =
    ghost_runtime_observable::matches(pack_verdict_v<at::ghost, at::with<Es...>>.premises);

static_assert(!ghost_with_row_matches<Effect::Init>,
              "Init must stay outside the observable set.  Moving it in rejects every ghost binding that "
              "participates in initialization, so it needs its own corpus entry naming the contradiction it "
              "catches.");
static_assert(!ghost_with_row_matches<Effect::Test>,
              "Test must stay outside the observable set.  A specification evaluated under a test harness is "
              "legitimate ghost code.");
static_assert(ghost_with_row_matches<Effect::Alloc>, "Alloc must stay inside the observable set.");
static_assert(ghost_with_row_matches<Effect::IO>, "IO must stay inside the observable set.");
static_assert(ghost_with_row_matches<Effect::Block>, "Block must stay inside the observable set.");
static_assert(ghost_with_row_matches<Effect::Bg>, "Bg must stay inside the observable set.");
static_assert(ghost_with_row_matches<Effect::Init, Effect::Block>, "one observable effect is enough");
static_assert(!ghost_with_row_matches<>, "the empty grade names no effect");
static_assert(ghost_runtime_observable::matches(pack_verdict_v<at::ghost, at::with_io>.premises),
              "a stated with<IO> names the observable effect IO, as the row Row<IO> does");
static_assert(!ghost_runtime_observable::matches(pack_verdict_v<at::with_io>.premises), "a binding with no ghost");

// The Internal tier, a stale_to grade and the ghost grade, as the matchers
// read them.  A carrier, a public value and a constant-time value are not
// Internal.
static_assert(pack_verdict_v<at::as_internal>.premises.internal && !pack_verdict_v<at::as_secret>.premises.internal
              && !pack_verdict_v<at::as_public>.premises.internal
              && !pack_verdict_v<at::constant_time>.premises.internal && !pack_verdict_v<>.premises.internal);
static_assert(pack_verdict_v<at::stale_to<5>>.premises.stale && !pack_verdict_v<at::ghost>.premises.stale
              && !pack_verdict_v<>.premises.stale);
static_assert(pack_verdict_v<at::ghost>.premises.ghost && !pack_verdict_v<at::copy>.premises.ghost);
static_assert(pack_verdict_v<at::as_secret>.premises.classified_on_bg
              && !pack_verdict_v<at::as_public>.premises.classified_on_bg);
static_assert(
    pack_verdict_v<at::as_secret>.premises.classified_on_staleness
    && !pack_verdict_v<at::declassify<::fixy::tags::secret_policy::AuthorizedReplay>>.premises.classified_on_staleness);
static_assert(pack_verdict_v<at::with_io>.premises.row_io && !pack_verdict_v<at::with_bg>.premises.row_io);
}  // namespace premise_cells

namespace detail {
struct corpus_header_site_ final {};
}  // namespace detail

static_assert(corpus_is_joined<detail::corpus_header_site_>());

namespace detail::corpus_self_test {

// Every entry is a diagnostic tag whose name is its own class name, so
// the string the message carries cannot drift from the type the
// instantiation trail names.  Each carries a citation, and its full
// diagnostic names it.  The identifier of a reflection has static storage
// and a zero after its last character, so the static text door gives it as a
// TextView.
template <class Entry>
[[nodiscard]] consteval bool entry_is_well_formed_() noexcept {
    return ::foundation::diag::is_diagnostic_class_v<Entry>
        && Entry::name == ::foundation::core::mint_static_text(std::meta::identifier_of(^^Entry).data())
        && Entry::description.size() != 0 && Entry::remediation.size() != 0 && Entry::cite().size() != 0
        && ::foundation::core::starts_with(Entry::full_diagnostic(), "fixy::fn<")
        && ::foundation::core::find(Entry::full_diagnostic(), Entry::name).is_some()
        && ::foundation::core::find(Entry::full_diagnostic(), Entry::cite()).is_some();
}

[[nodiscard]] consteval bool every_entry_is_well_formed_() noexcept {
    bool all_well_formed = true;
    static constexpr ::foundation::reflect::ReflectionList entries =
        ::foundation::reflect::reflection_list(std::meta::template_arguments_of(std::meta::dealias(^^Entries)));
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
static_assert(!IsInCorpus<int>);
static_assert(std::is_same_v<matched_entry_or_void_t<int>, void>);

// Reject by default on the Security axis.  An IO row with no Security
// atom is a classified value on an observable channel, and is refused;
// the same pack naming the public grade is admitted.
static_assert(IsInCorpus<int, ::fixy::atom::with_io>);
static_assert(std::is_same_v<matched_entry_or_void_t<int, ::fixy::atom::with_io>, classified_io_without_declassify>);
static_assert(!IsInCorpus<int, ::fixy::atom::with_io, ::fixy::atom::as_public>);

// A declassification licenses the channels its policy names, and no
// other.  The wire policy licenses IO.  The replay policy does not, so
// the same IO row under it is classified IO.  No policy licenses Bg.
static_assert(
    !IsInCorpus<int, ::fixy::atom::with_io, ::fixy::atom::declassify<::fixy::tags::secret_policy::WireSerialize>>);
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
static_assert(
    IsInCorpus<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>, ::fixy::atom::stale_to<5>>);
static_assert(!IsInCorpus<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuthorizedReplay>,
                          ::fixy::atom::stale_to<5>>);

// The name and the diagnostic read off a matched pack, and are empty
// off an admitted one.
static_assert(corpus_entry_name_for_v<int, ::fixy::atom::with_io> == "classified_io_without_declassify");
static_assert(
    ::foundation::core::find(corpus_full_diagnostic_v<int, ::fixy::atom::with_io>, "Sabelfeld-Myers 2003").is_some());
static_assert(corpus_entry_name_for_v<int>.size() == 0);
static_assert(corpus_full_diagnostic_v<int>.size() == 0);

}  // namespace detail::corpus_self_test

}  // namespace fixy::corpus
