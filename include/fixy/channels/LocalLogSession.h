#pragma once

// The session of a log channel: one producer pole that appends records and
// one consumer pole that drains them, each a carrier of the Local network
// (fixy/session/NetworkModel.h).  The log carries records and no label, so
// each side picks each branch alone: the producer picks when it stops
// appending, and the consumer picks when it stops draining.
//
// This header gives the pole table of such a channel and the side relation
// of a local choice (fixy/session/Admission.h).  The producer pole runs
//
//     Loop<Select<Send<value_type, Continue>, End>>
//
// and the consumer pole runs
//
//     Loop<Select<Recv<value_type, Continue>, End>>.
//
// The two protocols are not duals, because each side holds a Select.  The
// relation of a local choice admits them, so no pole of a log runs without a
// stated relation.  A local choice is no agreement: the sides do not agree on
// when to stop.  The producer can stop before the consumer drains the last
// record, and the consumer can stop with records in the log.  A consumer at
// its receive after the producer stopped finds no record, and its handle
// waits.  The steps of the two sides face each other only while both go on.
//
// A channel is a log when it names value_type, ProducerHandle and
// ConsumerHandle, and each of its two handles states the Local network.  The
// table knows the two handles of such a channel.  crucible/MetaLogSession.h
// opens the poles of the MetaLog through this table.  The write of the table
// appends one record with try_append_one_pure in the context of the step, so
// that context has a pure source.  The read drains one record with
// try_drain_one.  Each tries one time.

#include <fixy/session/Admission.h>
#include <fixy/session/Fold.h>
#include <fixy/session/NetworkModel.h>
#include <fixy/session/Protocol.h>

#include <concepts>
#include <meta>
#include <type_traits>

namespace fixy::session {

// ── The relation of a local choice ──────────────────────────────────
//
// Over the Local network a choice puts no label on the wire, so no peer can
// follow it, and each side makes its own choices.  The local dual of a
// protocol keeps each output choice an output choice, and each other
// combinator becomes its registered dual.  A protocol with an input choice or
// a keyed step has no local dual, because no label arrives to pick a branch,
// and no peer reads the word of a keyed step.  The local dual of the local
// dual of a protocol is that protocol, so the relation is symmetric.

namespace detail {

// The local dual as an algebra of the fold of fixy/session/Fold.h.  A child
// with no local dual gives none to its parent.
struct local_dual_algebra {
    using result = std::meta::info;
    using context = int;
    std::meta::info registry{};

    template <class Child>
    consteval std::meta::info terminal(const transition::node& view, context, const Child&) const {
        return nullary(view);
    }
    template <class Child>
    consteval std::meta::info back(const transition::node& view, context, const Child&) const {
        return nullary(view);
    }
    template <class Child>
    consteval std::meta::info step(const transition::node& view, context ctx, const Child& child) const {
        if (transition::is_keyed_step(registry, view)) return {};
        const std::meta::info next = child(view.next, ctx);
        if (next == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.dual, {view.payload, next});
    }
    template <class Child>
    consteval std::meta::info choice(const transition::node& view, context ctx, const Child& child) const {
        if (view.entry.direction != transition::polarity::output) return {};
        transition::stack<std::meta::info> mapped{};
        for (const std::meta::info branch : view.branches) {
            const std::meta::info local = child(branch, ctx);
            if (local == std::meta::info{}) return {};
            transition::push(mapped, local);
        }
        return transition::detail::rebuild_choice(registry, view, view.entry.shape, mapped);
    }
    template <class Child>
    consteval std::meta::info binder(const transition::node& view, context ctx, const Child& child) const {
        const std::meta::info next = child(view.next, ctx);
        if (next == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.dual, {next});
    }
    template <class Child>
    consteval std::meta::info wrapper(const transition::node& view, context ctx, const Child& child) const {
        const std::meta::info next = child(view.next, ctx);
        if (next == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.dual, {view.value, next});
    }
    template <class Child>
    consteval std::meta::info marker(const transition::node& view, context ctx, const Child& child) const {
        const std::meta::info next = child(view.next, ctx);
        if (next == std::meta::info{}) return {};
        return std::meta::substitute(view.entry.dual, {next});
    }
    // The local dual of a name that the local dual made is its first name.
    template <class Child>
    consteval std::meta::info named(const transition::node& view, context ctx, const Child& child) const {
        const std::meta::info source = transition::detail::folded_source(registry, view.name, *this, ctx);
        if (source != std::meta::info{}) return std::meta::substitute(view.entry.dual, {source});
        return transition::detail::fold_named(registry, view, view.entry.dual, child(view.next, ctx), *this, ctx);
    }
    consteval std::meta::info unregistered(const transition::node&, context) const { return {}; }

private:
    static consteval std::meta::info nullary(const transition::node& view) {
        return view.entry.dual == std::meta::info{} ? view.type : view.entry.dual;
    }
};

// The local dual of a protocol, or null when it has none.  Complexity: one
// fold, linear in the distinct nodes of the protocol.
[[nodiscard]] consteval std::meta::info local_dual_of(std::meta::info protocol) {
    return transition::fold(protocol_registry, protocol, local_dual_algebra{protocol_registry}, 0);
}

}  // namespace detail

// The relation of a local choice: each Resource is a carrier of the Local
// network, and the peer protocol is the local dual of the self protocol.
template <typename SelfProto, typename PeerProto, typename ResourceSelf, typename ResourcePeer>
concept LocalChoiceSidesAgree = LocalCarrier<ResourceSelf> && LocalCarrier<ResourcePeer>
                             && detail::local_dual_of(^^SelfProto) == std::meta::dealias(^^PeerProto);

// The family local_choice of the side relations.  The registration stands
// in the file of its concept.
namespace side_relations::local_choice {

inline constexpr side_relation local_choice{.relation = ^^LocalChoiceSidesAgree};

inline constexpr ::fixy::session::transition::seal relation_seal{.kind = ^^side_relation, .count = 1};

}  // namespace side_relations::local_choice

}  // namespace fixy::session

namespace fixy::channels {

// A channel whose two handles append and drain records, each over the Local
// network.
template <class Channel>
concept LocalLogChannel =
    requires {
        typename Channel::value_type;
        typename Channel::ProducerHandle;
        typename Channel::ConsumerHandle;
    } && ::fixy::session::LocalCarrier<typename Channel::ProducerHandle>
    && ::fixy::session::LocalCarrier<typename Channel::ConsumerHandle>;

// The pole table of a log channel (fixy/session/Admission.h, section The pole
// table of a channel).
template <class Pole>
    requires ::fixy::session::ChannelPole<Pole> && LocalLogChannel<::fixy::session::pole_channel_t<Pole>>
          && (std::same_as<Pole, typename ::fixy::session::pole_channel_t<Pole>::ProducerHandle>
              || std::same_as<Pole, typename ::fixy::session::pole_channel_t<Pole>::ConsumerHandle>)
struct log_pole_protocols {
    using value_type = typename ::fixy::session::pole_channel_t<Pole>::value_type;
    using producer_protocol = ::fixy::session::Loop<
        ::fixy::session::Select<::fixy::session::Send<value_type, ::fixy::session::Continue>, ::fixy::session::End>>;
    using consumer_protocol = ::fixy::session::Loop<
        ::fixy::session::Select<::fixy::session::Recv<value_type, ::fixy::session::Continue>, ::fixy::session::End>>;
    static constexpr bool is_producer =
        std::same_as<Pole, typename ::fixy::session::pole_channel_t<Pole>::ProducerHandle>;
    using self_protocol = std::conditional_t<is_producer, producer_protocol, consumer_protocol>;
    using peer_protocol = std::conditional_t<is_producer, consumer_protocol, producer_protocol>;

    template <::foundation::effects::IsExecCtx Ctx>
        requires is_producer && requires(Ctx const& ctx, Pole& pole, value_type const& value) {
            { pole.try_append_one_pure(ctx, value) } -> std::same_as<bool>;
        }
    [[nodiscard]] static constexpr bool write(Ctx const& ctx, Pole& pole, value_type& value) noexcept {
        return pole.try_append_one_pure(ctx, value);
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires(!is_producer) && requires(Pole& pole) {
            { pole.try_drain_one() } -> std::same_as<::foundation::core::Option<value_type>>;
        }
    [[nodiscard]] static constexpr ::foundation::core::Option<value_type> read(Ctx const& /*ctx*/,
                                                                               Pole& pole) noexcept {
        return pole.try_drain_one();
    }
};

}  // namespace fixy::channels

// The family of this header in the root of the pole tables.  The
// registration stands in the file of its template.
namespace fixy::session::pole_tables::log {

inline constexpr ::fixy::session::pole_table local_log{.table = ^^::fixy::channels::log_pole_protocols};

inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};

}  // namespace fixy::session::pole_tables::log
