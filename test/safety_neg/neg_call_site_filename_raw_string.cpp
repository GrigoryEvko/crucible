// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CallSiteTable::Entry stores its filename and funcname as InternedName,
// a fixy::Tagged<std::string, source::Interned>.  The constructor of
// Tagged is private, and mint_tagged is its door, so an Entry cannot hold
// a raw std::string that no call named a tag for.  This fixture builds an
// Entry with a raw filename.  Every other field goes through its door.
//
// The companion fixture neg_call_site_filename_external_passback covers
// the read side: a stored name does not convert to an External name.

#include <crucible/CallSiteTable.h>

#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

#include <cstdint>
#include <string>

int main() {
    crucible::CallSiteTable::Entry e{crucible::CallsiteHash{uint64_t{0xC0FFEE}}, std::string{"file.py"},
                                     ::fixy::mint_tagged<::fixy::tags::source::Interned>(std::string{"f"}),
                                     ::fixy::mint_refined<::fixy::non_negative>(int32_t{42})};
    (void)e;
    return 0;
}
