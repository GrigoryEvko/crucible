// A memset over a class that holds a plain Result writes each byte of the
// tag of the Result, and a byte of 0xFF names no alternative.  The cells of
// the tag carry the mark closed_tag, so the quarantine plugin refuses the
// fill of the class that holds them.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Code : std::uint8_t {
    closed,
};

struct Reply {
    std::uint32_t sequence = 0;
    ::foundation::core::Result<std::uint32_t, Code> outcome{std::uint32_t{1}};
};

int main() {
    Reply reply{};
    __builtin_memset(static_cast<void*>(&reply), 0xFF, sizeof(reply));
    return reply.outcome.is_ok() ? 1 : 0;
}
