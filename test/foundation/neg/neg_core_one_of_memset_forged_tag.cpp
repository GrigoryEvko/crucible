// A memset over a class that holds a plain OneOf writes each byte of the tag
// of the OneOf, and a byte of 0xFF names no alternative.  The cells of the
// tag carry the mark closed_tag, so the quarantine plugin refuses the fill of
// the class that holds them.

#include <foundation/core/OneOf.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed,
};

struct Frame {
    std::uint32_t sequence = 0;
    ::foundation::core::OneOf<std::uint32_t, Code> reading{std::uint32_t{1}};
};

int main() {
    Frame frame{};
    __builtin_memset(static_cast<void*>(&frame), 0xFF, sizeof(frame));
    return frame.reading.is<Code>() ? 1 : 0;
}
