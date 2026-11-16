// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CallSiteTable stores each name as InternedName, because it keeps one
// copy for each hash and runs no sanitizer.  Sanitized is an earned tag:
// only a retag along a discharge edge of fixy/Tagged.h reaches it, and no
// edge leaves Interned.  So a stored name, which may hold External bytes,
// cannot come out as a Sanitized name.  This fixture tries that retag on a
// name of the type that the table stores.

#include <crucible/CallSiteTable.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

#include <utility>

int main() {
    crucible::CallSiteTable::InternedName stored =
        ::fixy::mint_tagged<::fixy::tags::source::Interned>(::fixy::freeze(::fixy::mint_grow_text(16)));
    auto laundered = std::move(stored).retag<::fixy::tags::source::Sanitized>();
    (void)laundered;
    return 0;
}
