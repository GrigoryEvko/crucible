// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CallSiteTable::Entry stores its filename and funcname as InternedName,
// a fixy::Tagged<fixy::HeapText, source::Interned>.  External and internal
// names go in, and an Interned name comes out.  Two tags of Tagged do not
// convert, so a stored name cannot go back to an API that takes an
// External name of the same text type.  This fixture tries that call.
//
// The companion fixture neg_call_site_filename_raw_string covers the
// write side: an Entry cannot hold a raw text.

#include <crucible/CallSiteTable.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

using ExternalName = ::fixy::Tagged<::fixy::HeapText, ::fixy::tags::source::External>;

void wants_external_only(ExternalName const&);

int main() {
    crucible::CallSiteTable t;
    (void)t.insert(::foundation::effects::testing::test().alloc,
                   ::fixy::mint_refined<::fixy::non_zero>(crucible::CallsiteHash{uint64_t{0xC0FFEE}}), "file.py", "f",
                   int32_t{42});
    wants_external_only(t.entries[0].filename);
    return 0;
}
