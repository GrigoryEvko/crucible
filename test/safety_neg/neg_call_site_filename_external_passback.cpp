// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CallSiteTable::Entry stores its filename and funcname as InternedName,
// a fixy::Tagged<std::string, source::Interned>.  External and internal
// names go in, and an Interned name comes out.  Two tags of Tagged do not
// convert, so a stored name cannot go back to an API that takes an
// External name.  This fixture tries that call.
//
// The companion fixture neg_call_site_filename_raw_string covers the
// write side: an Entry cannot hold a raw std::string.

#include <crucible/CallSiteTable.h>

#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

#include <cstdint>
#include <string>

using ExternalName = ::fixy::Tagged<std::string, ::fixy::tags::source::External>;

void wants_external_only(ExternalName const&);

int main() {
    crucible::CallSiteTable t;
    t.insert(::fixy::mint_refined<::fixy::non_zero>(crucible::CallsiteHash{uint64_t{0xC0FFEE}}), std::string{"file.py"},
             std::string{"f"}, int32_t{42});
    wants_external_only(t.entries[0].filename);
    return 0;
}
