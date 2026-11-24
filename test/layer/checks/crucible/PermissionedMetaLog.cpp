// The compile-time checks of crucible/PermissionedMetaLog.h.

#include <crucible/PermissionedMetaLog.h>

#include <foundation/Linear.h>

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

// A scope brand that the checks name, and the frame handles on it.  No frame
// door gives a handle on it.
struct CheckScope {};
using Producer = MetaLogProducerHandle<WitnessTag, WitnessBrand, CheckScope>;
using Consumer = MetaLogConsumerHandle<WitnessTag, WitnessBrand, CheckScope>;
static_assert(std::is_same_v<Witness::producer_handle<CheckScope>, Producer>);
static_assert(std::is_same_v<Witness::consumer_handle<CheckScope>, Consumer>);

// The log holds its MetaLog in a held split, so it is not Sendable.  A handle
// names the tag and the brand of its log and not the log, so a body can move
// each handle to the thread of its role.
static_assert(!::foundation::transfer::Sendable<Witness>);
static_assert(::foundation::transfer::Sendable<Producer> && ::foundation::transfer::Sendable<Consumer>);
static_assert(Witness::channel_form == ::fixy::concurrent::ChannelForm::wire);

// No code outside the frame doors opens an end: each claim door takes the
// key of the frame doors.
template <class Log>
concept OpensAnEndWithNoKey = requires(Log& log) {
    log.producer(std::declval<::foundation::permissions::Permission<typename Log::producer_tag, WitnessBrand>&&>());
};
static_assert(!OpensAnEndWithNoKey<Witness>);
static_assert(!std::is_constructible_v<Producer, Witness::ProducerEnd&&>);

// An assignment would drop the token of the target endpoint.
static_assert(!std::is_move_assignable_v<Witness::ProducerEnd> && !std::is_move_assignable_v<Witness::ConsumerEnd>);
static_assert(!std::is_move_assignable_v<Producer> && !std::is_move_assignable_v<Consumer>);

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
static_assert(!gives_a_record_address<Witness::ProducerEnd>());
static_assert(!gives_a_record_address<Witness::ConsumerEnd>());

// Each end and each handle holds a linear token, so each is linear, and
// release() gives the token back.  A handle marks its own end, so the drop
// door ends it.
static_assert(::foundation::linear::is_marked_linear(^^Witness::ProducerEnd)
              && ::foundation::linear::is_marked_linear(^^Witness::ConsumerEnd));
static_assert(::foundation::linear::DroppableLinear<Producer> && ::foundation::linear::DroppableLinear<Consumer>);
static_assert(std::is_same_v<decltype(std::declval<Producer>().release()),
                             ::foundation::permissions::Permission<Witness::producer_tag, WitnessBrand>>);
static_assert(std::is_same_v<decltype(std::declval<Consumer>().release()),
                             ::foundation::permissions::Permission<Witness::consumer_tag, WitnessBrand>>);

// The check finds a member that returns a reference to a record.
struct ReferenceReader {
    [[nodiscard]] const TensorMeta& at(MetaIndex index) const;
};
static_assert(gives_a_record_address<ReferenceReader>());

}  // namespace detail::metalog_self_test

}  // namespace crucible
