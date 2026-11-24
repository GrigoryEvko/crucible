// Only an atomic cell makes the key that turns a word into an AddressKey, so
// no code makes a key of an address from a word.

#include <foundation/core/Ref.h>

int main() {
    using Word = ::foundation::core::atomic_word<::foundation::core::AddressKey>;
    static_cast<void>(
        Word::from_word(::foundation::core::atomic_word_key<::foundation::core::AddressKey>{}, Word::empty_word));
    return 0;
}
