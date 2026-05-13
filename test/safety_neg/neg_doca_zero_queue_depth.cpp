#include <crucible/cntp/_wip/Doca.h>

namespace doca = crucible::cntp::_wip::doca;

constexpr doca::DocaQueueDepth bad_queue_depth{std::uint16_t{0}};

int main() {
    return static_cast<int>(bad_queue_depth.value());
}
