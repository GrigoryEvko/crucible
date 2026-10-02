// The compile-time checks of crucible/PermissionedMetaLog.h.

#include <crucible/PermissionedMetaLog.h>

#include <meta>

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

// True when a public member function of Owner returns a reference or a
// pointer to a TensorMeta.  After the consumer releases a record, the
// producer writes a new record into its slot.  So each reader of the log
// copies a record, and no member function of the log or of an endpoint gives
// the address of one.
template <class Owner>
consteval bool gives_a_record_address() {
    for (const std::meta::info member : std::meta::members_of(^^Owner, std::meta::access_context::current())) {
        if (!std::meta::is_function(member) || std::meta::is_constructor(member) || std::meta::is_destructor(member)) {
            continue;
        }
        const std::meta::info result = std::meta::return_type_of(member);
        if (!std::meta::is_reference_type(result) && !std::meta::is_pointer_type(result)) continue;
        const std::meta::info target = std::meta::is_pointer_type(result) ? std::meta::remove_pointer(result)
                                                                          : std::meta::remove_reference(result);
        if (std::meta::dealias(std::meta::remove_cv(target)) == ^^TensorMeta) return true;
    }
    return false;
}

static_assert(!gives_a_record_address<MetaLog>());
static_assert(!gives_a_record_address<Witness::ProducerHandle>());
static_assert(!gives_a_record_address<Witness::ConsumerHandle>());

// The check finds a member that returns a reference to a record.
struct ReferenceReader {
    [[nodiscard]] const TensorMeta& at(MetaIndex index) const;
};
static_assert(gives_a_record_address<ReferenceReader>());

}  // namespace detail::metalog_self_test

}  // namespace crucible
