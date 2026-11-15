// A union with a plain Option and a member of bytes reads the bytes of the
// second member as an Option, and those bytes can hold a tag that names no
// alternative.  The cells of the tag carry the mark closed_tag, so the
// quarantine plugin refuses the member of the union.

#include <foundation/core/Choice.h>

#include <cstdint>

struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

union Punned {
    ForgedWords words;
    ::foundation::core::Option<std::uint64_t> option;
};

int main() {
    Punned const punned{ForgedWords{}};
    return punned.option.is_some() ? 1 : 0;
}
