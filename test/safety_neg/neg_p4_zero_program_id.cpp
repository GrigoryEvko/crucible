#include <crucible/cntp/P4.h>

namespace p4 = crucible::cntp::p4;

constexpr p4::P4ProgramId bad_program_id{std::uint64_t{0}};

int main() {
    return static_cast<int>(bad_program_id.value());
}
