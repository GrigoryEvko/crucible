#include <crucible/observe/HdrHistogram.h>

namespace crucible::observe {

template class HdrHistogram<3, 3600000000000ull>;
template class ConcurrentHdrHistogram<3, 3600000000000ull, 4>;

}  // namespace crucible::observe
