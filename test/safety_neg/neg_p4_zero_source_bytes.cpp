#include <crucible/cntp/_wip/P4.h>

namespace p4 = crucible::cntp::_wip::p4;

constexpr p4::P4SourceBytes bad_source_bytes{std::uint64_t{0}};

int main() {
    return static_cast<int>(bad_source_bytes.value());
}
