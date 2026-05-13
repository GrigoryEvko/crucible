#include <crucible/cntp/_wip/P4.h>

namespace p4 = crucible::cntp::_wip::p4;

constexpr p4::P4TcamEntries bad_tcam_entries{std::uint32_t{0}};

int main() {
    return static_cast<int>(bad_tcam_entries.value());
}
