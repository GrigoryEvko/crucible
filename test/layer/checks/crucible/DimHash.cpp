// The compile-time checks of crucible/DimHash.h.

#include <crucible/DimHash.h>

namespace crucible {

static_assert(sizeof(DimHash) == sizeof(uint64_t), "Tagged<uint64_t, hash_family::FamilyB> must stay the width of "
                                                   "its payload so the dim hash stays register-sized");
static_assert(sizeof(DimHashDet) == sizeof(uint64_t), "DetSafe<Pure, Tagged<uint64_t, hash_family::FamilyB>> must "
                                                      "stay the width of its payload so the dim hash stays "
                                                      "register-sized");
static_assert(std::is_trivially_copyable_v<DimHash>);
static_assert(std::is_trivially_copyable_v<DimHashDet>);
static_assert(std::is_standard_layout_v<DimHash>);
static_assert(std::is_standard_layout_v<DimHashDet>);

}  // namespace crucible
