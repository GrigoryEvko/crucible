// A class that holds a plain Option as a member holds the tag of the Option
// in its bytes.  The walk of the checked lifetime start reads each member,
// and the cells of the tag carry the mark closed_tag, so the walk refuses the
// class too.

#include <foundation/Lifetime.h>
#include <foundation/core/Choice.h>

#include <cstdint>

struct Record {
    std::uint32_t count = 0;
    ::foundation::core::Option<std::uint32_t> slot;
};

struct RecordBytes {
    std::uint64_t first = 0;
    std::uint64_t second = 0x0000000200000007u;
};

int main() {
    RecordBytes bytes{};
    static_cast<void>(::foundation::lifetime::start_as_array<Record>(&bytes, 1));
    return 0;
}
