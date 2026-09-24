// A Sanitized path is the proof that a predicate ran.  Minting one
// straight from a raw filesystem path would be the forgery the whole
// mechanism exists to prevent, so the mint refuses a tag the catalog
// reaches only through retag.

#include <fixy/Path.h>

#include <filesystem>

int main() {
    fixy::Path<fixy::tags::source::Sanitized> forged{std::filesystem::path{"../etc/passwd"}};
    return forged.value().empty() ? 0 : 1;
}
