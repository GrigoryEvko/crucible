// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CallSiteTable stores each name as InternedName, because it keeps one
// copy for each hash and runs no sanitizer.  Sanitized is an earned tag:
// only a retag along a discharge edge of fixy/Tagged.h reaches it, and no
// edge leaves Interned.  So a stored name, which may hold External bytes,
// cannot come out as a Sanitized name.  This fixture tries that retag.

#include <crucible/CallSiteTable.h>

#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

#include <cstdint>
#include <string>
#include <utility>

int main() {
    crucible::CallSiteTable t;
    t.insert(::fixy::mint_refined<::fixy::non_zero>(crucible::CallsiteHash{uint64_t{0xC0FFEE}}),
             ::fixy::mint_tagged<::fixy::tags::source::External>(std::string{"file.py"}),
             ::fixy::mint_tagged<::fixy::tags::source::External>(std::string{"f"}), int32_t{42});
    crucible::CallSiteTable::InternedName stored = t.entries[0].filename;
    auto laundered = std::move(stored).retag<::fixy::tags::source::Sanitized>();
    (void)laundered;
    return 0;
}
