#include <crucible/observe/HdrHistogram.h>

namespace crucible::observe {

template class HdrHistogram<3, 3'600'000'000'000ull>;
template class ConcurrentHdrHistogram<3, 3'600'000'000'000ull, 4>;

}  // namespace crucible::observe
