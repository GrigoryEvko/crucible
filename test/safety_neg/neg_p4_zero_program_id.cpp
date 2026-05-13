#include <crucible/cntp/_wip/P4.h>

namespace p4 = crucible::cntp::_wip::p4;

constexpr p4::P4ProgramId bad_program_id{std::uint64_t{0}};

int main() {
    return static_cast<int>(bad_program_id.value());
}
