// The compile-time checks of crucible/PermissionedMetaLog.h.

#include <crucible/PermissionedMetaLog.h>

namespace crucible {

namespace detail::metalog_self_test {

struct WitnessTag {};
struct WitnessBrand {};

// A log names its tag and a brand of one root.  A log that omits the tag
// or the brand names no type, and no log is on the erased brand.
template <template <typename, ::foundation::brand::IsFreshBrand> class Log, typename... TagAndBrand>
concept NamesALogOf = requires { typename Log<TagAndBrand...>; };
static_assert(NamesALogOf<PermissionedMetaLog, WitnessTag, WitnessBrand>);
static_assert(!NamesALogOf<PermissionedMetaLog>);
static_assert(!NamesALogOf<PermissionedMetaLog, WitnessTag>);
static_assert(!NamesALogOf<PermissionedMetaLog, WitnessTag, ::foundation::brand::DefaultBrand>);

using Witness = PermissionedMetaLog<WitnessTag, WitnessBrand>;
static_assert(
    std::is_same_v<permissioned_metalog_t<::foundation::permissions::Permission<Witness::whole_tag, WitnessBrand>>,
                   Witness>);

}  // namespace detail::metalog_self_test

}  // namespace crucible
