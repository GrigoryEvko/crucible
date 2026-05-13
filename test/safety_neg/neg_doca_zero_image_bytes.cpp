#include <crucible/cntp/Doca.h>

namespace doca = crucible::cntp::doca;

constexpr doca::DocaImageBytes bad_image_bytes{std::uint64_t{0}};

int main() {
    return static_cast<int>(bad_image_bytes.value());
}
