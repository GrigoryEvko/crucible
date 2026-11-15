// An anonymous union in a class holds a plain OneOf and a word.  A write of
// the word writes the tag of the OneOf, and the word can hold a tag that names
// no alternative.  The cells of the tag carry the mark closed_tag, so the
// quarantine plugin refuses the read of the OneOf member.

#include <foundation/core/OneOf.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed,
};

struct Frame {
    union {
        std::uint64_t raw = 0;
        ::foundation::core::OneOf<std::uint32_t, Code> reading;
    };
};

int main() {
    Frame frame{};
    frame.raw = 0x0000000900000005u;
    return frame.reading.is<Code>() ? 1 : 0;
}
