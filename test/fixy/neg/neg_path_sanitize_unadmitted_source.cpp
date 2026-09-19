// A path lane reaches the sanitizer only when the retag catalog admits
// its tag into Sanitized.  CipherPath has no such edge on purpose: its
// bytes never crossed an untrusted boundary, and keeping it out of the
// three external lanes is what stops operator-supplied bytes from
// reaching the directory-anchored open helpers.

#include <fixy/Path.h>

#include <filesystem>
#include <utility>

int main() {
    auto cipher_path = fixy::mint_tagged<fixy::tags::source::CipherPath>(std::filesystem::path{"/var/cipher"});
    auto sanitized = fixy::sanitize::path_traversal::sanitize_path_no_dotdot(std::move(cipher_path));
    return sanitized.has_value() ? 0 : 1;
}
