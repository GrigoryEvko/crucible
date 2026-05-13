#include <crucible/cntp/_wip/Doca.h>

namespace doca = crucible::cntp::_wip::doca;

constexpr doca::DocaProgramId bad_program_id{std::uint64_t{0}};

int main() {
    return static_cast<int>(bad_program_id.value());
}
