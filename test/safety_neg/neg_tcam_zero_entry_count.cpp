#include <crucible/cntp/Tcam.h>

namespace tcam = crucible::cntp::tcam;

constexpr tcam::TcamEntryCount bad_count{std::uint32_t{0}};

int main() {
    return static_cast<int>(bad_count.value());
}
