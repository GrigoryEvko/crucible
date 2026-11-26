#pragma once

// The trace ring and the metadata log of a test that drives a
// BackgroundThread with no Vigil.  The rig holds each in a held split
// (fixy/concurrent/SpscEnds.h), as the Vigil does.  The producer sides open
// with the proof of the rig, which only the rig builds, and the consumer
// sides open with the proofs of the drain stage and of the publish stage.  So
// the rig gives the constructor of a BackgroundThread the two consumer sides
// that it holds, and the test writes each op through the doors of the rig on
// one thread.
//
// RigRing holds a ring, RigLog holds a log, and TraceRig holds one of each.
// A test that runs some pipelines one after the other can give one RigLog to
// each of them, so it holds one log of 176 MiB and not one for each case.
//
// A rig must live longer than each BackgroundThread that holds its sides: a
// test declares the rig before the BackgroundThread.  The plugin refuses a
// static that reaches a loan key, so a rig is a static only in the PROBE
// region of a death test, as in test_transaction_owner.
//
// Local holds a ring or a log whose two sides open with the proofs of the
// test, for a test or a bench with no BackgroundThread.  OpenRing and OpenLog
// wrap a Local with one door for each door of the owner, and each door opens
// the side that the owner door takes.  record_local records an op through a
// local ring and a local log, as the Vigil does.

#include <crucible/BackgroundThread.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/ValidatedOp.h>
#include <crucible/Vigil.h>

#include <fixy/Core.h>
#include <fixy/concurrent/SpscEnds.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace crucible::test {

class TraceRig;
class RigRing;
class RigLog;
template <class Owner>
class Local;

// Records one op through the producer ends of a local ring and a local log
// (TraceRing::try_record).  Returns false when the ring is full.
// The definition follows the class Local.
[[nodiscard]] inline bool record_local(Local<TraceRing>& ring, Local<MetaLog>& log, ValidatedOp const& op,
                                       ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {},
                                       OpDepth depth = {}) noexcept;

// The proof of the producer of a rig.  Only the rig builds one, for the frame
// of one write.  The destructor is user-provided, so no byte route builds one.
class RigProducerProof final {
    RigProducerProof() noexcept {}
    friend class RigRing;
    friend class RigLog;
    friend class TraceRig;

public:
    RigProducerProof(RigProducerProof const&) = delete("a proof of a side lives for the call that made it");
    ~RigProducerProof() {}
};

// The two sides of the ring and of the log of a rig.
struct RigRingSides final {
    using producer_proof = RigProducerProof;
    using consumer_proof = BackgroundThread::DrainStage;
};
struct RigLogSides final {
    using producer_proof = RigProducerProof;
    using consumer_proof = BackgroundThread::PublishStage;
};

// The trace ring of a rig.
class RigRing final {
public:
    RigRing() noexcept
        : ring_{::fixy::concurrent::mint_held_spsc_split<TraceRing, RigRingSides>(
              ::foundation::effects::testing::test().alloc)} {}

    RigRing(RigRing const&) = delete("a BackgroundThread holds the side of a ring, so the ring does not copy");
    RigRing(RigRing&&) = delete("a BackgroundThread holds the side of a ring, so the ring does not move");

    // Appends one entry, with a meta start that the test gives.  Returns
    // false when the ring is full.
    [[nodiscard]] bool append_entry(TraceRing::Entry const& entry, MetaIndex meta_start = MetaIndex::none(),
                                    ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {},
                                    OpDepth depth = {}) noexcept {
        return ring_.producer_side().with_end(RigProducerProof{}, [&entry, meta_start, scope_hash, callsite_hash,
                                                                   depth](TraceRing::Producer& ring) noexcept {
            return TraceRing::try_append(ring, entry, meta_start, scope_hash, callsite_hash, depth);
        });
    }

    // The consumer side, which a BackgroundThread holds.
    [[nodiscard]] BackgroundThread::TraceRingSide& side() & noexcept { return ring_.consumer_side(); }

    // The counters, which any thread reads.
    [[nodiscard]] uint64_t total_produced() const noexcept {
        return ring_.with_owner([](TraceRing const& ring) noexcept { return ring.total_produced(); });
    }
    [[nodiscard]] ::fixy::Stale<uint32_t> size() const noexcept {
        return ring_.with_owner([](TraceRing const& ring) noexcept { return ring.size(); });
    }

private:
    friend class TraceRig;

    ::fixy::concurrent::SpscHeldSplit<TraceRing, RigRingSides> ring_;
};

// The metadata log of a rig.
class RigLog final {
public:
    // The log starts at the index start, so a test puts its first record near
    // the end of the buffer, or near the wrap of 32 bits.
    explicit RigLog(MetaIndex start = MetaIndex{0}) noexcept
        : log_{::fixy::concurrent::mint_held_spsc_split<MetaLog, RigLogSides>(
              ::foundation::effects::testing::test().alloc, ::foundation::effects::testing::test().alloc, start)} {}

    RigLog(RigLog const&) = delete("a BackgroundThread holds the side of a log, so the log does not copy");
    RigLog(RigLog&&) = delete("a BackgroundThread holds the side of a log, so the log does not move");

    // Appends a run of records, and gives the index of the first, or no
    // index when the log is full or the run is empty.
    [[nodiscard]] MetaIndex append_metas(::fixy::View<const TensorMeta> metas) noexcept {
        return log_.producer_side().with_end(
            RigProducerProof{}, [metas](MetaLog::Producer& log) noexcept { return MetaLog::try_append(log, metas); });
    }

    // The consumer side, which a BackgroundThread holds.
    [[nodiscard]] BackgroundThread::MetaLogSide& side() & noexcept { return log_.consumer_side(); }

    // The counters, which any thread reads.
    [[nodiscard]] uint64_t total_produced() const noexcept {
        return log_.with_owner([](MetaLog const& log) noexcept { return log.total_produced(); });
    }
    [[nodiscard]] uint64_t total_released() const noexcept {
        return log_.with_owner([](MetaLog const& log) noexcept { return log.total_released(); });
    }
    [[nodiscard]] ::fixy::Stale<uint32_t> size() const noexcept {
        return log_.with_owner([](MetaLog const& log) noexcept { return log.size(); });
    }

    // The log as const, for a test that reads its records or builds a trace
    // from it.  The test reads only records that it appended.
    [[nodiscard]] ::fixy::Ref<MetaLog const, ::fixy::concurrent::SpscSplitBrand<MetaLog>> view() & noexcept {
        return log_.consumer_side().owner_view();
    }

private:
    friend class TraceRig;

    ::fixy::concurrent::SpscHeldSplit<MetaLog, RigLogSides> log_;
};

// A ring or a log whose two sides the test opens itself, with no
// BackgroundThread.  It holds the owner in a held split whose two proofs only
// the doors of this header build, so each door below opens one side for its
// body, and with_ends opens the two sides in one frame, as for a reset.  A
// test that drives the two sides from two threads gives each thread its own
// door calls, and it states the reason with run_joined_raw_probe.
class LocalProducerProof final {
    LocalProducerProof() noexcept {}
    template <class>
    friend class Local;
    friend bool record_local(Local<TraceRing>&, Local<MetaLog>&, ValidatedOp const&, ScopeHash, CallsiteHash,
                             OpDepth) noexcept;

public:
    LocalProducerProof(LocalProducerProof const&) = delete("a proof of a side lives for the call that made it");
    ~LocalProducerProof() {}
};
class LocalConsumerProof final {
    LocalConsumerProof() noexcept {}
    template <class>
    friend class Local;

public:
    LocalConsumerProof(LocalConsumerProof const&) = delete("a proof of a side lives for the call that made it");
    ~LocalConsumerProof() {}
};
struct LocalSides final {
    using producer_proof = LocalProducerProof;
    using consumer_proof = LocalConsumerProof;
};

template <class Owner>
class Local final {
public:
    // The arguments after the allocation capability build the owner, such as
    // the start index of a log.
    template <class... Args>
    explicit Local(Args&&... args) noexcept
        : split_{::fixy::concurrent::mint_held_spsc_split<Owner, LocalSides>(
              ::foundation::effects::testing::test().alloc, static_cast<Args&&>(args)...)} {}

    Local(Local const&) = delete("a local owner holds the one owner of its test");
    Local(Local&&) = delete("a local owner holds the one owner of its test");

    // The body gets the producer end, the consumer end, or the two.
    template <class Body>
    auto with_producer(Body&& body) noexcept {
        return split_.producer_side().with_end(LocalProducerProof{}, static_cast<Body&&>(body));
    }
    template <class Body>
    auto with_consumer(Body&& body) noexcept {
        return split_.consumer_side().with_end(LocalConsumerProof{}, static_cast<Body&&>(body));
    }
    template <class Body>
    auto with_ends(Body&& body) noexcept {
        LocalProducerProof const producer{};
        LocalConsumerProof const consumer{};
        return ::fixy::concurrent::with_held_ends(split_.producer_side(), producer, split_.consumer_side(), consumer,
                                                  static_cast<Body&&>(body));
    }

    // The body gets the owner as const.
    template <class Body>
    auto with_owner(Body&& body) const noexcept {
        return split_.with_owner(static_cast<Body&&>(body));
    }

    // The owner as const, for a test that builds a trace from a log: the
    // view of the consumer side (owner_view of fixy/concurrent/SpscEnds.h).
    [[nodiscard]] ::fixy::Ref<Owner const, ::fixy::concurrent::SpscSplitBrand<Owner>> owner_view() & noexcept {
        return split_.consumer_side().owner_view();
    }

private:
    friend bool record_local(Local<TraceRing>&, Local<MetaLog>&, ValidatedOp const&, ScopeHash, CallsiteHash,
                             OpDepth) noexcept;

    ::fixy::concurrent::SpscHeldSplit<Owner, LocalSides> split_;
};

using LocalRing = Local<TraceRing>;
using LocalLog = Local<MetaLog>;

inline bool record_local(LocalRing& ring, LocalLog& log, ValidatedOp const& op, ScopeHash scope_hash,
                         CallsiteHash callsite_hash, OpDepth depth) noexcept {
    LocalProducerProof const proof{};
    return ::fixy::concurrent::with_held_ends(
        ring.split_.producer_side(), proof, log.split_.producer_side(), proof,
        [&op, scope_hash, callsite_hash, depth](TraceRing::Producer& ring_end, MetaLog::Producer& log_end) noexcept {
            return TraceRing::try_record(ring_end, log_end, op.metas(), op.entry(), scope_hash, callsite_hash, depth);
        });
}

// A local ring with a door of the test for each door of a side: each door
// opens the side for one call.  A test that drives a ring with no pipeline
// holds one of these in a Box, because the ring is 6 MiB.
class OpenRing final {
public:
    OpenRing() noexcept = default;

    [[nodiscard]] bool try_append(TraceRing::Entry const& entry, MetaIndex meta_start = MetaIndex::none(),
                                  ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {},
                                  OpDepth depth = {}) noexcept {
        return local_.with_producer(
            [&entry, meta_start, scope_hash, callsite_hash, depth](TraceRing::Producer& end) noexcept {
                return TraceRing::try_append(end, entry, meta_start, scope_hash, callsite_hash, depth);
            });
    }

    template <class Ctx>
    [[nodiscard]] bool try_append_pure(Ctx const& ctx, TraceRing::Entry const& entry,
                                       MetaIndex meta_start = MetaIndex::none(), ScopeHash scope_hash = {},
                                       CallsiteHash callsite_hash = {}, OpDepth depth = {}) noexcept {
        return local_.with_producer(
            [&ctx, &entry, meta_start, scope_hash, callsite_hash, depth](TraceRing::Producer& end) noexcept {
                return TraceRing::try_append_pure(ctx, end, entry, meta_start, scope_hash, callsite_hash, depth);
            });
    }

    // The runs are the runs of the frames of the caller, which the drain
    // takes by move.  Each door erases the brand and the fixed extent of each
    // run before it opens the frame of the consumer side (erase_brand of
    // foundation/core/Region.h).  So the frame and the flattened body of the
    // drain are one instance for each set of element types, and not one for
    // each frame of a caller.
    template <class... Runs>
    [[nodiscard]] uint32_t drain(Runs... runs) noexcept {
        return drain_erased_(erased_run_(std::move(runs))...);
    }

    template <class Ctx, class... Runs>
    [[nodiscard]] uint32_t drain_pure(Ctx const& ctx, Runs... runs) noexcept {
        return drain_pure_erased_(ctx, erased_run_(std::move(runs))...);
    }

    template <class... Runs>
    [[nodiscard]] uint32_t try_pop_batch(Runs... runs) noexcept {
        return pop_batch_erased_(erased_run_(std::move(runs))...);
    }

    // Empties the ring, with the two ends in one frame.
    void reset() noexcept {
        local_.with_ends([](TraceRing::Producer& producer, TraceRing::Consumer& consumer) noexcept {
            TraceRing::reset(producer, consumer);
        });
    }

    [[nodiscard]] ::fixy::Stale<uint32_t> size() const noexcept {
        return local_.with_owner([](TraceRing const& ring) noexcept { return ring.size(); });
    }
    [[nodiscard]] uint64_t total_produced() const noexcept {
        return local_.with_owner([](TraceRing const& ring) noexcept { return ring.total_produced(); });
    }

private:
    // The run on the erased identity with a dynamic extent.
    template <class T, class Brand, std::size_t Extent>
    [[nodiscard]] static auto erased_run_(::fixy::ViewMut<T, Brand, Extent>&& run) noexcept {
        auto erased = ::fixy::erase_brand(std::move(run));
        if constexpr (Extent == ::fixy::dynamic_extent) {
            return erased;
        } else {
            return ::fixy::ViewMut<T, typename decltype(erased)::brand_type>{std::move(erased)};
        }
    }

    // The frames of the three drains, on the erased runs.  The body holds
    // each erased run by value: the store check refuses a body that writes an
    // erased run of its caller, because each erased run has one type.
    template <class... Erased>
    [[nodiscard]] uint32_t drain_erased_(Erased... runs) noexcept {
        return local_.with_consumer([... held = std::move(runs)](TraceRing::Consumer& end) mutable noexcept {
            return TraceRing::drain(end, std::move(held)...);
        });
    }

    template <class Ctx, class... Erased>
    [[nodiscard]] uint32_t drain_pure_erased_(Ctx const& ctx, Erased... runs) noexcept {
        return local_.with_consumer([&ctx, ... held = std::move(runs)](TraceRing::Consumer& end) mutable noexcept {
            return TraceRing::drain_pure(ctx, end, std::move(held)...);
        });
    }

    template <class... Erased>
    [[nodiscard]] uint32_t pop_batch_erased_(Erased... runs) noexcept {
        return local_.with_consumer([... held = std::move(runs)](TraceRing::Consumer& end) mutable noexcept {
            return TraceRing::try_pop_batch(end, std::move(held)...);
        });
    }

    Local<TraceRing> local_{};
};

// A local log with a door of the test for each door of a side and each read
// of the owner, as for OpenRing.
class OpenLog final {
public:
    // The log starts at the index start.
    explicit OpenLog(MetaIndex start = MetaIndex{0}) noexcept
        : local_{::foundation::effects::testing::test().alloc, start} {}

    [[nodiscard]] MetaIndex try_append(::fixy::View<const TensorMeta> metas) noexcept {
        return local_.with_producer(
            [metas](MetaLog::Producer& end) noexcept { return MetaLog::try_append(end, metas); });
    }

    template <class Ctx>
    [[nodiscard]] MetaIndex try_append_pure(Ctx const& ctx, ::fixy::View<const TensorMeta> metas) noexcept {
        return local_.with_producer(
            [&ctx, metas](MetaLog::Producer& end) noexcept { return MetaLog::try_append_pure(ctx, end, metas); });
    }

    void advance_tail(uint64_t new_tail) noexcept {
        local_.with_consumer([new_tail](MetaLog::Consumer& end) noexcept { MetaLog::advance_tail(end, new_tail); });
    }

    // Empties the log, with the two ends in one frame.
    void reset() noexcept {
        local_.with_ends([](MetaLog::Producer& producer, MetaLog::Consumer& consumer) noexcept {
            MetaLog::reset(producer, consumer);
        });
    }

    // A record holds the data pointer of its tensor, and no frame gives such
    // a value out or writes it into a run of the caller, so each copy of a
    // record goes through the view of the consumer side.
    [[nodiscard]] TensorMeta copy_record(uint64_t index) const noexcept { return view_->copy_record(index); }
    void copy_run(uint64_t start, ::fixy::View<TensorMeta> dst) const noexcept { view_->copy_run(start, dst); }
    template <class Brand, std::size_t Extent>
    void copy_run(uint64_t start, ::fixy::ViewMut<TensorMeta, Brand, Extent> dst) const noexcept {
        view_->copy_run(start, std::move(dst));
    }
    [[nodiscard]] bool holds_record(TensorMeta const& record) const noexcept {
        return local_.with_owner([&record](MetaLog const& log) noexcept { return log.holds_record(record); });
    }
    [[nodiscard]] ::fixy::Stale<uint32_t> size() const noexcept {
        return local_.with_owner([](MetaLog const& log) noexcept { return log.size(); });
    }
    [[nodiscard]] uint64_t total_produced() const noexcept {
        return local_.with_owner([](MetaLog const& log) noexcept { return log.total_produced(); });
    }
    [[nodiscard]] uint64_t total_released() const noexcept {
        return local_.with_owner([](MetaLog const& log) noexcept { return log.total_released(); });
    }

    // The log as const, for a bench that builds a trace from it.
    [[nodiscard]] ::fixy::Ref<MetaLog const, ::fixy::concurrent::SpscSplitBrand<MetaLog>> view() const noexcept {
        return view_;
    }

private:
    Local<MetaLog> local_;
    // The view of the consumer side, which lives as long as local_.  A body
    // of a frame captures the log by a const reference, so the Ref gives no
    // slot that a body writes.
    ::fixy::Ref<MetaLog const, ::fixy::concurrent::SpscSplitBrand<MetaLog>> const view_{local_.owner_view()};
};

// A ring and a log.
class TraceRig final {
public:
    explicit TraceRig(MetaIndex log_start = MetaIndex{0}) noexcept : log_{log_start} {}

    TraceRig(TraceRig const&) = delete("a rig holds the one ring and the one log of its test");
    TraceRig(TraceRig&&) = delete("a BackgroundThread holds the sides of a rig, so the rig does not move");

    // Records one op through the producer sides of the ring and of the log
    // (TraceRing::try_record).  Returns false when the ring is full.
    [[nodiscard]] bool record(ValidatedOp const& op, ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {},
                              OpDepth depth = {}) noexcept {
        RigProducerProof const proof{};
        return ::fixy::concurrent::with_held_ends(
            ring_.ring_.producer_side(), proof, log_.log_.producer_side(), proof,
            [&op, scope_hash, callsite_hash, depth](TraceRing::Producer& ring, MetaLog::Producer& log) noexcept {
                return TraceRing::try_record(ring, log, op.metas(), op.entry(), scope_hash, callsite_hash, depth);
            });
    }

    [[nodiscard]] bool append_entry(TraceRing::Entry const& entry, MetaIndex meta_start = MetaIndex::none(),
                                    ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {},
                                    OpDepth depth = {}) noexcept {
        return ring_.append_entry(entry, meta_start, scope_hash, callsite_hash, depth);
    }
    [[nodiscard]] MetaIndex append_metas(::fixy::View<const TensorMeta> metas) noexcept {
        return log_.append_metas(metas);
    }

    [[nodiscard]] BackgroundThread::TraceRingSide& ring_side() & noexcept { return ring_.side(); }
    [[nodiscard]] BackgroundThread::MetaLogSide& log_side() & noexcept { return log_.side(); }

    [[nodiscard]] uint64_t ring_total_produced() const noexcept { return ring_.total_produced(); }
    [[nodiscard]] ::fixy::Stale<uint32_t> ring_size() const noexcept { return ring_.size(); }
    [[nodiscard]] uint64_t log_total_produced() const noexcept { return log_.total_produced(); }
    [[nodiscard]] uint64_t log_total_released() const noexcept { return log_.total_released(); }
    [[nodiscard]] ::fixy::Stale<uint32_t> log_size() const noexcept { return log_.size(); }
    [[nodiscard]] ::fixy::Ref<MetaLog const, ::fixy::concurrent::SpscSplitBrand<MetaLog>> log_view() & noexcept {
        return log_.view();
    }

private:
    RigRing ring_;
    RigLog log_;
};

}  // namespace crucible::test
