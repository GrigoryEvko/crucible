// A Sanitized path is the proof that a predicate ran.  Minting one
// straight from a raw filesystem path would be the forgery the whole
// mechanism exists to prevent, so the mint refuses a tag the catalog
// reaches only through retag.

#include <fixy/os/Path.h>

#include <foundation/os/PathText.h>

int main() {
    fixy::Path<fixy::tags::source::Sanitized> forged{foundation::os::PathText<fixy::kPathBytes>{}};
    return forged.value().size() == 0 ? 0 : 1;
}
