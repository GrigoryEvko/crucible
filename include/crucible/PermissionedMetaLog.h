#pragma once

// The MetaLog behind one linear Permission per endpoint.  The handle types
// carry the role, so appending from the consumer side or draining from the
// producer side does not compile, and the move-only Permission keeps the
// log to one producer and one consumer at a time.
//
// A log has the brand of the root that its permissions grow from.  The
// caller mints the whole root, names its brand in the log type, and
// splits the root into the producer token and the consumer token.  A root
// of another call site has another brand, so its tokens open no endpoint
// of this log.  One call site that runs two times mints two roots of one
// brand, and no type tells them apart.  A claim on each role
// (foundation/ChannelBinding.h) refuses a second live handle of one role
// there.
//
// Each handle is linear (foundation/Linear.h).  Its door is release(),
// which gives the role back to the wrapper and the token back to the
// caller.
//
// The log is a wire of the frame doors of fixy/concurrent/
// PermissionedChannel.h: with_producer, with_consumer and with_ends open its
// ends and give a body a handle of each, on the scope brand of the call, so
// no handle outlives the frame of its door.  The ends (ProducerEnd and
// ConsumerEnd) are nested classes, and the check of synchronized{} of
// foundation/Transfer.h reads their doors.  The type of an end stays
// spellable, but each claim door takes the key of the frame doors, so no
// code outside them gets a value of an end.  The residuals of that header
// hold here too: a log in a Box or an Option that the body ends, and an
// owner thread that keeps the closure past the body.

#include <crucible/MetaLog.h>

#include <fixy/Aliases.h>
#include <fixy/Core.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/session/NetworkModel.h>
#include <foundation/Brand.h>
#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

// The log is memory that the process already owns, so an endpoint that
// touches it incurs no effect: the three tags declare the empty row.
namespace metalog_tag {

template <typename UserTag>
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
template <typename UserTag>
struct Producer {
    using permission_row = ::foundation::effects::Row<>;
};
template <typename UserTag>
struct Consumer {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace metalog_tag

// The handles of the two roles (the frame doors).  The definitions follow the
// log.
template <typename UserTag, ::foundation::brand::IsFreshBrand Brand, ::foundation::brand::IsBrand Scope>
class MetaLogProducerHandle;
template <typename UserTag, ::foundation::brand::IsFreshBrand Brand, ::foundation::brand::IsBrand Scope>
class MetaLogConsumerHandle;

// UserTag and Brand have no default.  A default tag would give every log
// that omits it one set of Permission types.  The erased brand would let a
// token of every root site open the endpoints.
//
// The producer handle appends and the consumer handle drains on their threads
// at the same time by design.  The wrapper holds its log in a held split
// (fixy/concurrent/SpscEnds.h).  The producer handle opens the producer side
// with the proof of its role, and the consumer handle opens the consumer side
// with the proof of its role, so each side has the one writer that the role
// claims and the Permissions of the handles name.  Each member is private,
// and only the handles reach the log, so the wrapper states synchronized{}
// (foundation/Transfer.h).
template <typename UserTag, ::foundation::brand::IsFreshBrand Brand>
class[[= ::foundation::transfer::synchronized{}]] PermissionedMetaLog {
public:
    using value_type = ::crucible::TensorMeta;
    using user_tag = UserTag;
    using brand_type = Brand;
    using whole_tag = metalog_tag::Whole<UserTag>;
    using producer_tag = metalog_tag::Producer<UserTag>;
    using consumer_tag = metalog_tag::Consumer<UserTag>;
    // The log has one producer and one consumer, as a wire has.
    static constexpr ::fixy::concurrent::ChannelForm channel_form = ::fixy::concurrent::ChannelForm::wire;

    // The wrapper builds its log with the allocation capability of the
    // caller, and the log is empty.
    explicit PermissionedMetaLog(::foundation::effects::Alloc allocation) noexcept
        : log_{::fixy::concurrent::mint_held_spsc_split<::crucible::MetaLog, LogSides>(allocation, allocation)} {}

    PermissionedMetaLog(const PermissionedMetaLog&) =
        delete("the wrapper holds one MetaLog, and the endpoints it made hold that log");
    PermissionedMetaLog& operator=(const PermissionedMetaLog&) =
        delete("the wrapper holds one MetaLog, and the endpoints it made hold that log");
    PermissionedMetaLog(PermissionedMetaLog&&) =
        delete("the wrapper holds one MetaLog, and the endpoints it made hold that log");
    PermissionedMetaLog&
    operator=(PermissionedMetaLog&&) = delete("the wrapper holds one MetaLog, and the endpoints it made hold that log");

private:
    // The proof of each role, which opens the side of that role.  Only the
    // handle of the role builds one, for the frame of one call.  The
    // destructor is user-provided, so no byte route builds a proof.
    class ProducerSideProof final {
        ProducerSideProof() noexcept {}
        friend class PermissionedMetaLog;

    public:
        ProducerSideProof(ProducerSideProof const&) = delete("a proof of a side lives for the call that made it");
        ~ProducerSideProof() {}
    };
    class ConsumerSideProof final {
        ConsumerSideProof() noexcept {}
        friend class PermissionedMetaLog;

    public:
        ConsumerSideProof(ConsumerSideProof const&) = delete("a proof of a side lives for the call that made it");
        ~ConsumerSideProof() {}
    };
    // The held brand of the log: the proof of each role opens its side.
    struct LogSides final {
        using producer_proof = ProducerSideProof;
        using consumer_proof = ConsumerSideProof;
    };

public:
    class[[= ::foundation::linear::linear{}]] ProducerEnd {
        // The move clears the binding, so a moved-from handle cannot
        // append or drain through the Permission it no longer holds.  The
        // binding names the wrapper, which holds the log and the claims.
        ::foundation::ChannelBinding<PermissionedMetaLog> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<producer_tag, Brand> perm_;

        constexpr ProducerEnd(PermissionedMetaLog& wrapper,
                              ::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept
            : ch_{wrapper, ::foundation::BindingKey<PermissionedMetaLog>{}}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type = producer_tag;
        using brand_type = Brand;
        using channel_type = PermissionedMetaLog;

        // The producer picks alone when it appends and when it stops, and no
        // peer reads a label, so a session over the handle states the Local
        // network (fixy/session/NetworkModel.h).
        static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::Local;

        ProducerEnd(const ProducerEnd&) =
            delete("MetaLog ProducerEnd owns the Producer Permission — copy would duplicate the linear token");
        ProducerEnd& operator=(const ProducerEnd&) =
            delete("MetaLog ProducerEnd owns the Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerEnd(ProducerEnd&&) noexcept = default;
        ProducerEnd& operator=(ProducerEnd&&) = delete(
            "MetaLog ProducerEnd binds to one MetaLog for life — rebinding would orphan the original Permission");

        // A moved-from handle holds no claim, so only the bound one gives
        // the role back.
        ~ProducerEnd() {
            if (ch_.is_bound()) ch_->producer_claim_.release();
        }

        // The release door.  The handle gives the producer role back to the
        // wrapper and the token back to the caller, and it is unbound after
        // the call.  The token can open a producer handle again.
        [[nodiscard]] ::foundation::permissions::Permission<producer_tag, Brand> release() && noexcept {
            ch_->producer_claim_.release();
            ch_.unbind();
            return std::move(perm_);
        }

        // Each append takes the context of the caller, and the gate is this
        // constraint: the row of the context is empty, and its source permits
        // no atom, so a context narrowed with in_row fails it.  The handle has
        // no append with no context, so no caller skips the gate.  The body
        // calls MetaLog::try_append on the producer end, so the gate does not
        // depend on the gate of MetaLog itself.
        template <::foundation::effects::IsExecCtx Ctx>
            requires ::fixy::IsPure<::foundation::effects::row_type_of_t<Ctx>>
                  && ::foundation::effects::CtxHasPureSource<Ctx>
        [[nodiscard, gnu::hot]] ::crucible::MetaIndex try_append_pure(Ctx const&,
                                                                      ::fixy::View<const value_type> metas) {
            // A View of more records than the log holds is a mistake of the
            // caller, and the checked door of a run ends the process for it.
            return ch_->log_.producer_side().with_end(
                ProducerSideProof{},
                [run = ::crucible::mint_checked_meta_run(metas)](::crucible::MetaLog::Producer& end) noexcept {
                    return ::crucible::MetaLog::try_append(end, run);
                });
        }

        // The append of one record, with the same gate.  The record goes into
        // an array of one record, whose View of a fixed extent the log copies
        // with no check of its count.
        template <::foundation::effects::IsExecCtx Ctx>
            requires ::fixy::IsPure<::foundation::effects::row_type_of_t<Ctx>>
                  && ::foundation::effects::CtxHasPureSource<Ctx>
        [[nodiscard, gnu::hot]] bool try_append_one_pure(Ctx const&, const value_type& meta) {
            return ch_->log_.producer_side().with_end(
                ProducerSideProof{}, [&meta](::crucible::MetaLog::Producer& end) noexcept {
                    const ::fixy::FixedArray<value_type, 1> record{meta};
                    return ::crucible::MetaLog::try_append(end, ::crucible::mint_meta_run(record.view())).is_valid();
                });
        }

        // A snapshot reads the index of the consumer, and the schedule decides
        // when the consumer moves it.  So the snapshot takes the bare tag of
        // Nondet.
        [[nodiscard]] std::uint32_t size_approx(::foundation::effects::cap::Nondet) const {
            return ch_->log_.with_owner([](::crucible::MetaLog const& log) noexcept { return log.size().peek(); });
        }
    };

    // The tail comes from the copy of the consumer end of the log, with no
    // load of the counter.  The consumer permission is linear, so the thread
    // holding it is the only writer of the tail.  The head index is read with
    // get, which acquires against the producer's release and is what makes
    // the appended records visible.
    class[[= ::foundation::linear::linear{}]] ConsumerEnd {
        ::foundation::ChannelBinding<PermissionedMetaLog> ch_;
        [[no_unique_address]] ::foundation::permissions::Permission<consumer_tag, Brand> perm_;

        constexpr ConsumerEnd(PermissionedMetaLog& wrapper,
                              ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept
            : ch_{wrapper, ::foundation::BindingKey<PermissionedMetaLog>{}}, perm_{std::move(perm)} {}
        friend class PermissionedMetaLog;

        // The tail, from the copy of the consumer end (MetaLog::released).
        [[nodiscard]] ::crucible::MetaIndex released_() {
            return ch_->log_.consumer_side().with_end(
                ConsumerSideProof{},
                [](::crucible::MetaLog::Consumer& end) noexcept { return ::crucible::MetaLog::released(end); });
        }

    public:
        using value_type = ::crucible::TensorMeta;
        using tag_type = consumer_tag;
        using brand_type = Brand;
        using channel_type = PermissionedMetaLog;

        // The consumer picks alone when it drains and when it stops, so a
        // session over the handle states the Local network.
        static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::Local;

        ConsumerEnd(const ConsumerEnd&) =
            delete("MetaLog ConsumerEnd owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerEnd& operator=(const ConsumerEnd&) =
            delete("MetaLog ConsumerEnd owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerEnd(ConsumerEnd&&) noexcept = default;
        ConsumerEnd& operator=(ConsumerEnd&&) = delete(
            "MetaLog ConsumerEnd binds to one MetaLog for life — rebinding would orphan the original Permission");

        ~ConsumerEnd() {
            if (ch_.is_bound()) ch_->consumer_claim_.release();
        }

        // The release door, as for ProducerEnd.
        [[nodiscard]] ::foundation::permissions::Permission<consumer_tag, Brand> release() && noexcept {
            ch_->consumer_claim_.release();
            ch_.unbind();
            return std::move(perm_);
        }

        // The reads of the consumer go through the view of the consumer side
        // (owner_view of fixy/concurrent/SpscEnds.h), because a record holds
        // the data pointer of its tensor, and no frame gives such a value
        // out.  The consumer reads each record between the tail that only it
        // moves and the head that it acquired, so each read is of a record
        // that the producer published and that no release freed.  The tail
        // is the copy of the consumer end (released_), so the release steps
        // from the value of that read.
        [[nodiscard, gnu::hot]] ::fixy::Option<value_type> try_drain_one() {
            const auto log = ch_->log_.consumer_side().owner_view();
            const ::crucible::MetaIndex t = released_();
            if (t == log->total_produced()) [[unlikely]] {
                return ::fixy::none;
            }

            const value_type meta = log->copy_record(t);
            advance_tail(::fixy::WideStep<::crucible::MetaLog::CAPACITY>::one());
            return ::fixy::some(meta);
        }

        template <typename Body>
            requires std::is_invocable_v<Body&, const value_type&>
        [[nodiscard]] std::uint32_t drain(Body&& body, std::uint32_t max_items = ::crucible::MetaLog::CAPACITY) {
            const auto log = ch_->log_.consumer_side().owner_view();
            const ::crucible::MetaIndex t = released_();
            const std::uint64_t available = log->total_produced().raw() - t.raw();
            const std::uint32_t count = max_items < available ? max_items : static_cast<std::uint32_t>(available);

            // The body gets a copy of each record, and no reference into a
            // slot that the producer writes again after the release.
            for (std::uint32_t i = 0; i < count; ++i) {
                const value_type meta = log->copy_record(::crucible::MetaIndex{t.raw() + i});
                body(meta);
            }
            if (count != 0) {
                // The log holds at most CAPACITY records, so the count of a
                // drain is a step of the tail.
                advance_tail(::fixy::WideStep<::crucible::MetaLog::CAPACITY>::check_within_bound(count));
            }
            return count;
        }

        // Copies the record at index and leaves the tail where it is.  The
        // index must name a record that the log holds, from the tail up to
        // the head.  A record before the tail is free, and the producer can
        // write a new record into its slot.  The test of the live window
        // reads two loads, which an assumption cannot hold, so it is a fatal
        // invariant and checks under each contract semantic.
        [[nodiscard]] value_type copy_at(::crucible::MetaIndex index) const {
            CRUCIBLE_PRE(index.is_valid());
            const auto log = ch_->log_.consumer_side().owner_view();
            CRUCIBLE_FATAL_INVARIANT(::crucible::is_at_or_after(index, log->total_released())
                                     && ::crucible::is_before(index, log->total_produced()));
            return log->copy_record(index);
        }

        // Releases the step records after the tail (MetaLog::advance_tail).
        void advance_tail(::fixy::WideStep<::crucible::MetaLog::CAPACITY> step) {
            ch_->log_.consumer_side().with_end(
                ConsumerSideProof{},
                [step](::crucible::MetaLog::Consumer& end) noexcept { ::crucible::MetaLog::advance_tail(end, step); });
        }

        [[nodiscard]] ::crucible::MetaIndex head_index() const {
            return ch_->log_.with_owner([](::crucible::MetaLog const& log) noexcept { return log.total_produced(); });
        }

        [[nodiscard]] ::crucible::MetaIndex tail_index() const {
            return ch_->log_.with_owner([](::crucible::MetaLog const& log) noexcept { return log.total_released(); });
        }

        // A snapshot reads the index of the producer, so it takes the bare tag
        // of Nondet, as the snapshot of the producer does.
        [[nodiscard]] std::uint32_t size_approx(::foundation::effects::cap::Nondet) const {
            return ch_->log_.with_owner([](::crucible::MetaLog const& log) noexcept { return log.size().peek(); });
        }
    };

    // The handles that the frame doors give a body on the scope brand Scope.
    template <::foundation::brand::IsBrand Scope>
    using producer_handle = MetaLogProducerHandle<UserTag, Brand, Scope>;
    template <::foundation::brand::IsBrand Scope>
    using consumer_handle = MetaLogConsumerHandle<UserTag, Brand, Scope>;

    // The claim doors of the ends.  Each takes the key of the frame doors, so
    // only a frame door opens an end.  Each takes the claim of its role first,
    // so a second live handle of the role ends the process here.
    [[nodiscard]] ProducerEnd producer(::fixy::concurrent::ChannelFrameKey const& /*key*/,
                                       ::foundation::permissions::Permission<producer_tag, Brand>&& perm) noexcept {
        producer_claim_.take("two live producer handles of one MetaLog. One call site minted the root of the log "
                             "two times.");
        return ProducerEnd{*this, std::move(perm)};
    }

    [[nodiscard]] ConsumerEnd consumer(::fixy::concurrent::ChannelFrameKey const& /*key*/,
                                       ::foundation::permissions::Permission<consumer_tag, Brand>&& perm) noexcept {
        consumer_claim_.take("two live consumer handles of one MetaLog. One call site minted the root of the log "
                             "two times.");
        return ConsumerEnd{*this, std::move(perm)};
    }

    [[nodiscard]] static constexpr bool is_exclusive_active() noexcept { return false; }

private:
    ::fixy::concurrent::SpscHeldSplit<::crucible::MetaLog, LogSides> log_;
    ::foundation::EndpointClaim producer_claim_;
    ::foundation::EndpointClaim consumer_claim_;
};

// The producer handle of a frame of a log (the frame doors): the producer
// end on the scope brand of one call of a frame door.  The handle derives
// from its end privately, so no code outside it binds a reference to the end,
// and it shows each door of the end.  The constructor is private, and the key
// of the frame doors is its one friend, so only a frame door builds a handle.
// The handle takes the tag and the brand of its log, and not the log.  The
// log holds its MetaLog in a held split, which states no transfer fact, so
// the log is not Sendable, and rule 8 of foundation/Transfer.h reads each
// type argument of a specialization.  The
// tag and the brand hold no state, so the handle is Sendable when its end is.
// No argument is linear, so the handle marks its own end (foundation/
// Linear.h).
template <typename UserTag, ::foundation::brand::IsFreshBrand Brand, ::foundation::brand::IsBrand Scope>
class[[= ::foundation::linear::linear{}]] MetaLogProducerHandle
    : private PermissionedMetaLog<UserTag, Brand>::ProducerEnd {
    using End = typename PermissionedMetaLog<UserTag, Brand>::ProducerEnd;

    explicit MetaLogProducerHandle(End&& end) noexcept : End{std::move(end)} {}
    friend class ::fixy::concurrent::ChannelFrameKey;

public:
    using value_type = typename End::value_type;
    using tag_type = typename End::tag_type;
    using brand_type = typename End::brand_type;
    using channel_type = typename End::channel_type;
    // The scope brand of the frame of the handle.
    using scope_type = Scope;
    static constexpr ::fixy::session::Network session_network = End::session_network;

    MetaLogProducerHandle(MetaLogProducerHandle&&) noexcept = default;
    MetaLogProducerHandle(MetaLogProducerHandle const&) =
        delete("a MetaLog producer handle owns the producer permission, and a copy would duplicate the linear token");
    MetaLogProducerHandle&
    operator=(MetaLogProducerHandle const&) = delete("a MetaLog producer handle owns the producer permission");
    MetaLogProducerHandle&
    operator=(MetaLogProducerHandle&&) = delete("a MetaLog producer handle binds to one MetaLog for its life");
    ~MetaLogProducerHandle() = default;

    using End::release;
    using End::size_approx;
    using End::try_append_one_pure;
    using End::try_append_pure;
};

// The consumer handle of a frame of a log (the frame doors), as
// MetaLogProducerHandle is for the producer.
template <typename UserTag, ::foundation::brand::IsFreshBrand Brand, ::foundation::brand::IsBrand Scope>
class[[= ::foundation::linear::linear{}]] MetaLogConsumerHandle
    : private PermissionedMetaLog<UserTag, Brand>::ConsumerEnd {
    using End = typename PermissionedMetaLog<UserTag, Brand>::ConsumerEnd;

    explicit MetaLogConsumerHandle(End&& end) noexcept : End{std::move(end)} {}
    friend class ::fixy::concurrent::ChannelFrameKey;

public:
    using value_type = typename End::value_type;
    using tag_type = typename End::tag_type;
    using brand_type = typename End::brand_type;
    using channel_type = typename End::channel_type;
    // The scope brand of the frame of the handle.
    using scope_type = Scope;
    static constexpr ::fixy::session::Network session_network = End::session_network;

    MetaLogConsumerHandle(MetaLogConsumerHandle&&) noexcept = default;
    MetaLogConsumerHandle(MetaLogConsumerHandle const&) =
        delete("a MetaLog consumer handle owns the consumer permission, and a copy would duplicate the linear token");
    MetaLogConsumerHandle&
    operator=(MetaLogConsumerHandle const&) = delete("a MetaLog consumer handle owns the consumer permission");
    MetaLogConsumerHandle&
    operator=(MetaLogConsumerHandle&&) = delete("a MetaLog consumer handle binds to one MetaLog for its life");
    ~MetaLogConsumerHandle() = default;

    using End::advance_tail;
    using End::copy_at;
    using End::drain;
    using End::head_index;
    using End::release;
    using End::size_approx;
    using End::tail_index;
    using End::try_drain_one;
};

namespace detail {

template <typename Root>
struct metalog_root_parts;

template <typename UserTag, typename Brand>
struct metalog_root_parts<::foundation::permissions::Permission<metalog_tag::Whole<UserTag>, Brand>> {
    using user_tag = UserTag;
    using brand = Brand;
};

}  // namespace detail

// The log of a whole root of type Root.  The alias reads the user tag and
// the brand off the root.
template <typename Root>
using permissioned_metalog_t = PermissionedMetaLog<typename detail::metalog_root_parts<Root>::user_tag,
                                                   typename detail::metalog_root_parts<Root>::brand>;

}  // namespace crucible

// Both the binary and the variadic split forms are specialized, so a caller
// can reach for either one.  The authoring witnesses are deliberately
// redundant: a forged can_split_into alone is caught by the missing witness
// beside it.

namespace foundation::permissions {

template <typename UserTag>
struct can_split_into<::crucible::metalog_tag::Whole<UserTag>, ::crucible::metalog_tag::Producer<UserTag>,
                      ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct can_split_into_pack<::crucible::metalog_tag::Whole<UserTag>, ::crucible::metalog_tag::Producer<UserTag>,
                           ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_authoring_witness<::crucible::metalog_tag::Whole<UserTag>, ::crucible::metalog_tag::Producer<UserTag>,
                                   ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

template <typename UserTag>
struct has_split_pack_authoring_witness<::crucible::metalog_tag::Whole<UserTag>,
                                        ::crucible::metalog_tag::Producer<UserTag>,
                                        ::crucible::metalog_tag::Consumer<UserTag>> : std::true_type {};

}  // namespace foundation::permissions
