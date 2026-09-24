// Corpus entry internal_bg_without_declassify.  Bell-LaPadula 1973,
// Smith-Volpano 1998, Sabelfeld-Myers 2003: the concurrent form of
// no-write-down for the Internal tier.  An org-internal value crossing
// into a background context makes that context's scheduling
// Internal-tier-dependent, and the spawn is a scheduler-observable
// event, so the crossing needs an audit-trail discharge.
//
// As with the IO form, as_internal is named because Internal sits below
// the strict pole.  The remediation is to drop the Bg atom and run on
// the foreground thread, where scheduling is deterministic, or to
// project Security to public: no shipping policy discharges Bg.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::as_internal, ::fixy::atom::with_bg> refused{};
    return 0;
}
