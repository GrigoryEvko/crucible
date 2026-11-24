// The ends of an SPSC owner (fixy/concurrent/SpscEnds.h): the mint builds the
// owner, the split that holds it with its two ends, and the token of each
// side.  A body borrows an end through a frame door of the split, and the side
// operations take the end.  A split of an owner that two threads write opens
// its two sides on two threads, and a held split opens each side with its
// proof.

#include <fixy/concurrent/SpscEnds.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::concurrent::ConsumerEnd;
using ::fixy::concurrent::ProducerEnd;
using ::fixy::concurrent::SpscSide;

// A log of counts with one producer and one consumer.  The producer state is
// the count that the producer wrote, and the consumer state is the count that
// the consumer read.  The owner keeps only the published counts.
class CountLog final {
public:
    struct producer_state {
        std::uint64_t written = 0;
    };
    struct consumer_state {
        std::uint64_t read = 0;
    };

    explicit CountLog(std::uint64_t capacity) noexcept : capacity_{capacity} {}
    CountLog(CountLog const&) = delete("a log is pinned to its two ends");
    CountLog(CountLog&&) = delete("a log is pinned to its two ends");
    CountLog& operator=(CountLog const&) = delete("a log is pinned to its two ends");
    CountLog& operator=(CountLog&&) = delete("a log is pinned to its two ends");
    ~CountLog() = default;

    // Writes one item, or gives false when the log is full.
    [[nodiscard]] static bool try_write(ProducerEnd<CountLog>& end) noexcept {
        CountLog& log = *end.owner_;
        if (end.state_.written - log.published_read_ >= log.capacity_) return false;
        end.state_.written += 1;
        log.published_written_ = end.state_.written;
        return true;
    }

    // Reads one item, or gives false when the log is empty.
    [[nodiscard]] static bool try_read(ConsumerEnd<CountLog>& end) noexcept {
        CountLog& log = *end.owner_;
        if (end.state_.read == log.published_written_) return false;
        end.state_.read += 1;
        log.published_read_ = end.state_.read;
        return true;
    }

    // The count of items of the log that the two published counts give.
    [[nodiscard]] std::uint64_t held() const noexcept { return published_written_ - published_read_; }

    // True when the two ends name this log.
    [[nodiscard]] bool is_owner_of(ProducerEnd<CountLog> const& producer,
                                   ConsumerEnd<CountLog> const& consumer) const noexcept {
        return ::fixy::same_object(producer.owner_, ::fixy::to(*this))
            && ::fixy::same_object(consumer.owner_, ::fixy::to(*this));
    }

private:
    std::uint64_t capacity_ = 0;
    std::uint64_t published_written_ = 0;
    std::uint64_t published_read_ = 0;
};

// An end is a Ref of its owner and the state of its side.
using EndRef = ::fixy::Ref<CountLog, ::fixy::concurrent::SpscSplitBrand<CountLog>>;
static_assert(sizeof(ProducerEnd<CountLog>) == sizeof(EndRef) + sizeof(CountLog::producer_state));
static_assert(sizeof(ConsumerEnd<CountLog>) == sizeof(EndRef) + sizeof(CountLog::consumer_state));
static_assert(ProducerEnd<CountLog>::side == SpscSide::producer && ConsumerEnd<CountLog>::side == SpscSide::consumer);
static_assert(std::is_same_v<ConsumerEnd<CountLog>::state_type, CountLog::consumer_state>);

// The places of the ends of an owner and of the states of its two ends: each
// end takes the next place.  A test reads them around the end of a split.
int g_end_place = 0;
int g_owner_end_place = 0;
int g_producer_end_place = 0;
int g_consumer_end_place = 0;

// An owner whose end and whose two states record their places.
class OrderLog final {
public:
    struct producer_state {
        ~producer_state() { g_producer_end_place = ++g_end_place; }
    };
    struct consumer_state {
        ~consumer_state() { g_consumer_end_place = ++g_end_place; }
    };

    OrderLog() noexcept = default;
    OrderLog(OrderLog const&) = delete("a log is pinned to its two ends");
    OrderLog(OrderLog&&) = delete("a log is pinned to its two ends");
    ~OrderLog() { g_owner_end_place = ++g_end_place; }
};

// The split holds the Box first, so its two ends end before the owner that
// their Ref names: the consumer end, then the producer end, then the owner.
// The order is in the type of the split, and no holder can change it.
int check_the_ends_end_before_the_owner() {
    {
        [[maybe_unused]] auto minted =
            ::fixy::concurrent::mint_spsc_split<OrderLog>(::foundation::effects::testing::test().alloc);
    }
    if (g_consumer_end_place != 1 || g_producer_end_place != 2) return 1;
    if (g_owner_end_place != 3) return 2;
    return 0;
}

// Reads each item that the log holds, through the consumer end.
[[nodiscard]] std::uint64_t read_all(ConsumerEnd<CountLog>& end) noexcept {
    std::uint64_t count = 0;
    while (CountLog::try_read(end))
        ++count;
    return count;
}

// The held count of the log of a split.
template <class Split>
[[nodiscard]] std::uint64_t held_of(Split const& split) noexcept {
    return with_owner(split, [](CountLog const& log) noexcept { return log.held(); });
}

int check_split() {
    auto [log, producer_token, consumer_token] =
        ::fixy::concurrent::mint_spsc_split<CountLog>(::foundation::effects::testing::test().alloc, std::uint64_t{3});

    // The two ends name the log of the split.  The body reads the split
    // through a const reference, so it stores no borrow in the split.
    auto const& reader = log;
    bool const is_owner =
        with_ends(log, producer_token, consumer_token,
                  [&reader](ProducerEnd<CountLog> const& producer, ConsumerEnd<CountLog> const& consumer) noexcept {
                      return with_owner(reader, [&producer, &consumer](CountLog const& owner) noexcept {
                          return owner.is_owner_of(producer, consumer);
                      });
                  });
    if (!is_owner) return 1;

    // The producer end keeps its count, and the log keeps the published
    // count.  The borrow form of the door keeps the token with the caller.
    std::uint64_t const written = with_producer(log, producer_token, [](ProducerEnd<CountLog>& end) noexcept {
        std::uint64_t count = 0;
        while (CountLog::try_write(end))
            ++count;
        return count;
    });
    if (written != 3) return 2;
    if (held_of(log) != 3) return 3;

    // The move form gives the token back after the result.
    auto [read, consumer_back] = with_consumer(log, std::move(consumer_token),
                                               [](ConsumerEnd<CountLog>& end) noexcept { return read_all(end); });
    if (read != 3 || held_of(log) != 0) return 4;

    // A body that gives void gets the token back alone, and the end keeps its
    // state across two frames.
    producer_token = with_producer(log, std::move(producer_token), [](ProducerEnd<CountLog>& end) noexcept {
        static_cast<void>(CountLog::try_write(end));
    });
    if (held_of(log) != 1) return 5;
    if (with_consumer(log, consumer_back, [](ConsumerEnd<CountLog>& end) noexcept { return read_all(end); }) != 1) {
        return 6;
    }
    return 0;
}

// A log that starts at a position: each end starts with the first state that
// the log gives, so the counts of the ends agree with the published counts of
// the log (the first states).
class StartLog final {
public:
    struct producer_state {
        std::uint64_t written = 0;
    };
    struct consumer_state {
        std::uint64_t read = 0;
    };

    explicit StartLog(std::uint64_t start) noexcept : published_written_{start}, published_read_{start} {}
    StartLog(StartLog const&) = delete("a log is pinned to its two ends");
    StartLog(StartLog&&) = delete("a log is pinned to its two ends");

    [[nodiscard]] static producer_state first_producer_state(StartLog const& log) noexcept {
        return producer_state{.written = log.published_written_};
    }
    [[nodiscard]] static consumer_state first_consumer_state(StartLog const& log) noexcept {
        return consumer_state{.read = log.published_read_};
    }

    [[nodiscard]] static std::uint64_t written_of(ProducerEnd<StartLog> const& end) noexcept {
        return end.state_.written;
    }
    [[nodiscard]] static std::uint64_t read_of(ConsumerEnd<StartLog> const& end) noexcept { return end.state_.read; }

private:
    std::uint64_t published_written_ = 0;
    std::uint64_t published_read_ = 0;
};
static_assert(::fixy::concurrent::SpscGivesFirstStates<StartLog>
              && !::fixy::concurrent::SpscGivesFirstStates<CountLog>);

int check_first_states() {
    constexpr std::uint64_t kStart = std::uint64_t{0xFFFFFFFE};
    auto [log, producer_token, consumer_token] =
        ::fixy::concurrent::mint_spsc_split<StartLog>(::foundation::effects::testing::test().alloc, kStart);
    bool const starts_at_the_position =
        with_ends(log, producer_token, consumer_token,
                  [](ProducerEnd<StartLog> const& producer, ConsumerEnd<StartLog> const& consumer) noexcept {
                      return StartLog::written_of(producer) == kStart && StartLog::read_of(consumer) == kStart;
                  });
    return starts_at_the_position ? 0 : 1;
}

// A count that two threads publish: the producer publishes its count with a
// release, and the consumer reads it with an acquire.  Each published count
// is a cell of the base, so two threads can write the log at the same time,
// and its split states synchronized{}.
class CellLog final {
public:
    struct producer_state {
        std::uint64_t written = 0;
    };
    struct consumer_state {
        std::uint64_t read = 0;
    };

    CellLog() noexcept = default;
    CellLog(CellLog const&) = delete("a log is pinned to its two ends");
    CellLog(CellLog&&) = delete("a log is pinned to its two ends");

    // Publishes one more item.
    static void write(ProducerEnd<CellLog>& end) noexcept {
        end.state_.written += 1;
        end.owner_->published_.store_release(end.state_.written);
    }

    // Reads one item, or gives false when the log holds none.
    [[nodiscard]] static bool try_read(ConsumerEnd<CellLog>& end) noexcept {
        if (end.state_.read == end.owner_->published_.load_acquire()) return false;
        end.state_.read += 1;
        return true;
    }

private:
    ::foundation::core::Atomic<std::uint64_t> published_{std::uint64_t{0}};
};

template <class Split>
using SplitOf = std::remove_cvref_t<Split>;

constexpr std::uint64_t kItems = 4096;

// Two threads hold one side each: the producer token and the consumer token
// move into the body of their thread, and the two bodies run at the same
// time.  The consumer reads each item that the producer published, so the
// two counts agree.  The token of each body comes back from its door, and the
// end of the body ends it.
int check_two_threads() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto [split, producer_token, consumer_token] =
        ::fixy::concurrent::mint_spsc_split<CellLog>(::foundation::effects::testing::test().alloc);
    static_assert(::foundation::transfer::Synchronized<SplitOf<decltype(split)>>);
    auto producer = [&split, token = std::move(producer_token)]() mutable noexcept {
        auto back = with_producer(split, std::move(token), [](ProducerEnd<CellLog>& end) noexcept {
            for (std::uint64_t item = 0; item < kItems; ++item)
                CellLog::write(end);
        });
        static_cast<void>(back);
    };
    auto consumer = [&split, token = std::move(consumer_token)]() mutable noexcept {
        auto [read, back] = with_consumer(split, std::move(token), [](ConsumerEnd<CellLog>& end) noexcept {
            std::uint64_t count = 0;
            while (count < kItems) {
                if (CellLog::try_read(end)) {
                    ++count;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            return count;
        });
        static_cast<void>(back);
        return read;
    };
    // A refused start gives the two bodies back with their tokens, and the
    // test ends them through the door of a body that no thread ran.
    using Values = ::foundation::core::Record<::foundation::core::Unit, std::uint64_t>;
    using Refused = ::fixy::thread::RefusedStart<decltype(producer), decltype(consumer)>;
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(producer), std::move(consumer))
        .match([](Values results) noexcept { return results.at(::fixy::index<1>()) == kItems ? 0 : 2; },
               [](Refused refusal) noexcept {
                   auto [producer_body, consumer_body] = std::move(refusal.parts);
                   ::foundation::linear::end_unrun_body(std::move(producer_body),
                                                        ::foundation::linear::UnrunReason::start_refused);
                   ::foundation::linear::end_unrun_body(std::move(consumer_body),
                                                        ::foundation::linear::UnrunReason::start_refused);
                   return 1;
               });
}

// Runs body with two splits of one mint site that are alive at one time, and
// their tokens, which the body takes by value.  The template calls itself one
// time, so its two instantiations run one call of the mint, and the two splits
// have one brand and one type.
template <class Body, class... First>
void with_two_splits(Body& body, First&&... first) noexcept {
    auto [split, producer_token, consumer_token] =
        ::fixy::concurrent::mint_spsc_split<CellLog>(::foundation::effects::testing::test().alloc);
    if constexpr (sizeof...(First) == 0) {
        with_two_splits(body, split, std::move(producer_token), std::move(consumer_token));
    } else {
        body(std::forward<First>(first)..., split, std::move(producer_token), std::move(consumer_token));
    }
}

// The text of the fatal invariant that a token of a second split breaks.
constexpr ::fixy::TextView kForeignToken{"fatal invariant failed: token.split_ == identity_"};

// The tokens of two splits of one mint site have one type, so the types of
// the doors do not tell the splits apart.  The Identity of the split does:
// each door ends the process for a token of a second split, before it lends
// the end.  So no thread writes a side that a second token opens.  Each
// violation runs in a child process, which ends by the abort of the fatal
// invariant.
void token_of_a_second_split_ends_the_process(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            auto body = []([[maybe_unused]] auto& first_split, auto first_producer, auto first_consumer, auto& split,
                           auto producer, auto consumer) noexcept {
                static_assert(
                    std::is_same_v<std::remove_cvref_t<decltype(first_split)>, std::remove_cvref_t<decltype(split)>>);
                with_producer(split, first_producer, [](ProducerEnd<CellLog>&) noexcept {});
                static_cast<void>(producer);
                static_cast<void>(consumer);
                static_cast<void>(first_consumer);
            };
            with_two_splits(body);
        },
        kForeignToken);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            auto body = []([[maybe_unused]] auto& first_split, auto first_producer, auto first_consumer, auto& split,
                           auto producer, auto consumer) noexcept {
                auto back = with_producer(split, std::move(first_producer), [](ProducerEnd<CellLog>&) noexcept {});
                static_cast<void>(back);
                static_cast<void>(producer);
                static_cast<void>(consumer);
                static_cast<void>(first_consumer);
            };
            with_two_splits(body);
        },
        kForeignToken);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            auto body = []([[maybe_unused]] auto& first_split, auto first_producer, auto first_consumer, auto& split,
                           auto producer, auto consumer) noexcept {
                with_consumer(split, first_consumer, [](ConsumerEnd<CellLog>&) noexcept {});
                static_cast<void>(producer);
                static_cast<void>(consumer);
                static_cast<void>(first_producer);
            };
            with_two_splits(body);
        },
        kForeignToken);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            auto body = []([[maybe_unused]] auto& first_split, auto first_producer, auto first_consumer, auto& split,
                           auto producer, auto consumer) noexcept {
                auto back = with_consumer(split, std::move(first_consumer), [](ConsumerEnd<CellLog>&) noexcept {});
                static_cast<void>(back);
                static_cast<void>(producer);
                static_cast<void>(consumer);
                static_cast<void>(first_producer);
            };
            with_two_splits(body);
        },
        kForeignToken);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            auto body = []([[maybe_unused]] auto& first_split, auto first_producer, auto first_consumer, auto& split,
                           auto producer, auto consumer) noexcept {
                // The token of one side of the two is foreign.
                with_ends(split, first_producer, consumer,
                          [](ProducerEnd<CellLog>&, ConsumerEnd<CellLog>&) noexcept {});
                static_cast<void>(producer);
                static_cast<void>(first_consumer);
            };
            with_two_splits(body);
        },
        kForeignToken);
}

// Two producer tokens of one mint site would open the one producer side of one
// split from two threads, and the two threads would write its end at one time.
// The foreign token ends the process at the door of its thread, before that
// thread writes.  The bodies run in a child process, as a death test.
void foreign_token_on_a_second_thread_ends_the_process(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            auto body = []([[maybe_unused]] auto& first_split, auto first_producer, auto first_consumer, auto& split,
                           auto producer, auto consumer) noexcept {
                ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
                static_cast<void>(consumer);
                static_cast<void>(first_consumer);
                auto wrong = [&split, token = std::move(first_producer)]() mutable noexcept {
                    auto back = with_producer(split, std::move(token), [](ProducerEnd<CellLog>& end) noexcept {
                        for (std::uint64_t item = 0; item < kItems; ++item)
                            CellLog::write(end);
                    });
                    static_cast<void>(back);
                };
                auto right = [&split, token = std::move(producer)]() mutable noexcept {
                    auto back = with_producer(split, std::move(token), [](ProducerEnd<CellLog>& end) noexcept {
                        for (std::uint64_t item = 0; item < kItems; ++item)
                            CellLog::write(end);
                    });
                    static_cast<void>(back);
                };
                using Refused = ::fixy::thread::RefusedStart<decltype(wrong), decltype(right)>;
                ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(wrong), std::move(right))
                    .match([](::foundation::core::Unit) noexcept {},
                           [](Refused refusal) noexcept {
                               auto [wrong_body, right_body] = std::move(refusal.parts);
                               ::foundation::linear::end_unrun_body(std::move(wrong_body),
                                                                    ::foundation::linear::UnrunReason::start_refused);
                               ::foundation::linear::end_unrun_body(std::move(right_body),
                                                                    ::foundation::linear::UnrunReason::start_refused);
                           });
            };
            with_two_splits(body);
        },
        kForeignToken);
}

// The proofs of the two sides of a held split, and the door that makes them.
// A test makes each proof where it needs one.
class ProducerProof final {
    ProducerProof() noexcept = default;
    friend struct ProofDoor;

public:
    ProducerProof(ProducerProof const&) = delete("a proof of a side lives for the call that made it");
    ~ProducerProof() = default;
};
class ConsumerProof final {
    ConsumerProof() noexcept = default;
    friend struct ProofDoor;

public:
    ConsumerProof(ConsumerProof const&) = delete("a proof of a side lives for the call that made it");
    ~ConsumerProof() = default;
};
struct ProofDoor final {
    [[nodiscard]] static ProducerProof producer() noexcept { return ProducerProof{}; }
    [[nodiscard]] static ConsumerProof consumer() noexcept { return ConsumerProof{}; }
};

// A brand of a split that a class holds: it names the proof of each side.
struct HeldBrand final {
    using producer_proof = ProducerProof;
    using consumer_proof = ConsumerProof;
};

// A class that holds a split of the held brand as a member, built in place by
// the mint.  Each door of the class opens one side with its proof.
class LogHolder final {
public:
    explicit LogHolder(std::uint64_t capacity) noexcept
        : split_{::fixy::concurrent::mint_held_spsc_split<CountLog, HeldBrand>(
              ::foundation::effects::testing::test().alloc, capacity)} {}

    // Writes items until the log is full, and gives their count.
    [[nodiscard]] std::uint64_t write_all(ProducerProof const& proof) noexcept {
        return split_.producer_side().with_end(proof, [](ProducerEnd<CountLog>& end) noexcept {
            std::uint64_t count = 0;
            while (CountLog::try_write(end))
                ++count;
            return count;
        });
    }

    // Reads each item that the log holds, and gives their count.
    [[nodiscard]] std::uint64_t read_each(ConsumerProof const& proof) noexcept {
        return split_.consumer_side().with_end(proof,
                                               [](ConsumerEnd<CountLog>& end) noexcept { return read_all(end); });
    }

    [[nodiscard]] std::uint64_t held() const noexcept {
        return split_.with_owner([](CountLog const& log) noexcept { return log.held(); });
    }

    // The view of the owner that the consumer side gives.  It names the log
    // of the split, and it reads the count that a frame of the owner reads.
    [[nodiscard]] bool has_view_of_the_log() noexcept {
        ::foundation::core::Ref<CountLog const, ::fixy::concurrent::SpscSplitBrand<CountLog>> const view =
            split_.consumer_side().owner_view();
        bool const is_same_log = split_.with_owner(
            [&view](CountLog const& log) noexcept { return __builtin_addressof(*view) == __builtin_addressof(log); });
        return is_same_log && view->held() == held();
    }

private:
    ::fixy::concurrent::SpscHeldSplit<CountLog, HeldBrand> split_;
};

// A tally with one producer and one consumer: a second owner type for the
// door of two held sides.
class TallyLog final {
public:
    struct producer_state {
        std::uint64_t added = 0;
    };
    struct consumer_state {};

    TallyLog() noexcept = default;
    TallyLog(TallyLog const&) = delete("a tally is pinned to its two ends");
    TallyLog(TallyLog&&) = delete("a tally is pinned to its two ends");

    // Adds one to the tally.
    static void add(ProducerEnd<TallyLog>& end) noexcept {
        end.state_.added += 1;
        end.owner_->published_ = end.state_.added;
    }

    [[nodiscard]] std::uint64_t total() const noexcept { return published_; }

private:
    std::uint64_t published_ = 0;
};

// A class that holds two held splits of two owner types, whose brands name
// one producer proof, as the ring and the log of a Vigil do.  One door
// writes the two owners in one frame.
class PairHolder final {
public:
    PairHolder() noexcept
        : log_{::fixy::concurrent::mint_held_spsc_split<CountLog, HeldBrand>(
              ::foundation::effects::testing::test().alloc, std::uint64_t{4})},
          tally_{::fixy::concurrent::mint_held_spsc_split<TallyLog, HeldBrand>(
              ::foundation::effects::testing::test().alloc)} {}

    // Writes one item into the log and adds one to the tally, in one frame.
    [[nodiscard]] bool write_both(ProducerProof const& proof) noexcept {
        return with_held_ends(log_.producer_side(), proof, tally_.producer_side(), proof,
                              [](ProducerEnd<CountLog>& log, ProducerEnd<TallyLog>& tally) noexcept {
                                  if (!CountLog::try_write(log)) return false;
                                  TallyLog::add(tally);
                                  return true;
                              });
    }

    // Writes count items and reads each item of the log, with the producer
    // side and the consumer side of the one split open in one frame.
    [[nodiscard]] std::uint64_t write_and_read(ProducerProof const& producer, ConsumerProof const& consumer,
                                               std::uint64_t count) noexcept {
        return with_held_ends(log_.producer_side(), producer, log_.consumer_side(), consumer,
                              [count](ProducerEnd<CountLog>& writer, ConsumerEnd<CountLog>& reader) noexcept {
                                  for (std::uint64_t item = 0; item < count; ++item)
                                      static_cast<void>(CountLog::try_write(writer));
                                  return read_all(reader);
                              });
    }

    [[nodiscard]] std::uint64_t log_held() const noexcept {
        return log_.with_owner([](CountLog const& log) noexcept { return log.held(); });
    }
    [[nodiscard]] std::uint64_t tally() const noexcept {
        return tally_.with_owner([](TallyLog const& tally) noexcept { return tally.total(); });
    }

private:
    ::fixy::concurrent::SpscHeldSplit<CountLog, HeldBrand> log_;
    ::fixy::concurrent::SpscHeldSplit<TallyLog, HeldBrand> tally_;
};

int check_held_ends() {
    PairHolder holder{};
    ProducerProof const producer = ProofDoor::producer();
    if (!holder.write_both(producer) || !holder.write_both(producer)) return 1;
    if (holder.log_held() != 2 || holder.tally() != 2) return 2;

    // The two sides of one split open together on one thread: the reader
    // reads the two items from before the frame and the two of the frame.
    if (holder.write_and_read(producer, ProofDoor::consumer(), 2) != 4) return 3;
    if (holder.log_held() != 0) return 4;
    return 0;
}

int check_held_split() {
    LogHolder holder{std::uint64_t{2}};
    if (holder.write_all(ProofDoor::producer()) != 2) return 1;
    if (holder.held() != 2) return 2;

    // The ends keep their state across two frames of each side.
    if (holder.read_each(ProofDoor::consumer()) != 2 || holder.held() != 0) return 3;
    if (holder.write_all(ProofDoor::producer()) != 2) return 4;
    if (holder.read_each(ProofDoor::consumer()) != 2 || holder.held() != 0) return 5;

    // The consumer side gives a view of the log outside a frame.
    if (holder.write_all(ProofDoor::producer()) != 2 || !holder.has_view_of_the_log()) return 6;
    return 0;
}

// A class that two threads share, which holds a held split of a cell log
// (the held split).  Each side opens with a token that a claim door gives one
// time: the compare-exchange of the claim is the one atomic step, and only
// the thread that won it writes the end of its side.  The tokens are member
// classes, so they are proofs of this class, and each door of the class takes
// one.  The class keeps the split private, so only its own doors reach it.
class[[= ::foundation::transfer::synchronized{}]] CellPair final {
public:
    class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]][
        [= ::foundation::linear::scope_end_door{}]][[= ::foundation::transfer::not_shareable{}]] ProducerClaim final {
        friend class CellPair;
        ProducerClaim() noexcept {}

        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    public:
        ProducerClaim(ProducerClaim const&) = delete("a claim of a side has one holder");
        ProducerClaim& operator=(ProducerClaim const&) = delete("a claim of a side has one holder");
        ProducerClaim(ProducerClaim&&) noexcept = default;
        ProducerClaim& operator=(ProducerClaim&&) noexcept = default;
        ~ProducerClaim() = default;
    };
    class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]][
        [= ::foundation::linear::scope_end_door{}]][[= ::foundation::transfer::not_shareable{}]] ConsumerClaim final {
        friend class CellPair;
        ConsumerClaim() noexcept {}

        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    public:
        ConsumerClaim(ConsumerClaim const&) = delete("a claim of a side has one holder");
        ConsumerClaim& operator=(ConsumerClaim const&) = delete("a claim of a side has one holder");
        ConsumerClaim(ConsumerClaim&&) noexcept = default;
        ConsumerClaim& operator=(ConsumerClaim&&) noexcept = default;
        ~ConsumerClaim() = default;
    };
    struct Brand final {
        using producer_proof = ProducerClaim;
        using consumer_proof = ConsumerClaim;
    };

    CellPair() noexcept
        : split_{
              ::fixy::concurrent::mint_held_spsc_split<CellLog, Brand>(::foundation::effects::testing::test().alloc)} {}

    // The claim of the producer side: a token on the first call, and no value
    // on each later call.  The exchange is the one atomic step of the claim.
    [[nodiscard]] ::fixy::Option<ProducerClaim> claim_producer() noexcept {
        if (producer_claimed_.cas_acq_rel(false, true).is_err()) return ::fixy::none;
        return ::fixy::Option<ProducerClaim>::some(ProducerClaim{});
    }
    [[nodiscard]] ::fixy::Option<ConsumerClaim> claim_consumer() noexcept {
        if (consumer_claimed_.cas_acq_rel(false, true).is_err()) return ::fixy::none;
        return ::fixy::Option<ConsumerClaim>::some(ConsumerClaim{});
    }

    // Publishes count items through the producer side.
    void write(ProducerClaim const& claim, std::uint64_t count) noexcept {
        split_.producer_side().with_end(claim, [count](ProducerEnd<CellLog>& end) noexcept {
            for (std::uint64_t item = 0; item < count; ++item)
                CellLog::write(end);
        });
    }

    // Reads count items through the consumer side, with a wait for each item
    // that the producer did not publish yet.
    [[nodiscard]] std::uint64_t read(ConsumerClaim const& claim, std::uint64_t count) noexcept {
        return split_.consumer_side().with_end(claim, [count](ConsumerEnd<CellLog>& end) noexcept {
            std::uint64_t read = 0;
            while (read < count) {
                if (CellLog::try_read(end)) {
                    ++read;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            return read;
        });
    }

private:
    ::foundation::core::Atomic<bool> producer_claimed_{false};
    ::foundation::core::Atomic<bool> consumer_claimed_{false};
    ::fixy::concurrent::SpscHeldSplit<CellLog, Brand> split_;
};
static_assert(::foundation::transfer::Synchronized<CellPair>);

// Two threads hold one side of a held split each: each body claims its side,
// and the token of the claim opens it.  The two counts agree.
int check_held_two_threads() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    CellPair pair{};
    auto producer = [&pair]() noexcept {
        auto claim = pair.claim_producer().expect("the first claim of the producer side");
        pair.write(claim, kItems);
    };
    auto consumer = [&pair]() noexcept {
        auto claim = pair.claim_consumer().expect("the first claim of the consumer side");
        return pair.read(claim, kItems);
    };
    auto joined =
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(producer), std::move(consumer));
    if (joined.is_err()) return 1;
    auto results = std::move(joined).expect("the two threads joined");
    if (results.at(::fixy::index<1>()) != kItems) return 2;
    // Each side has one claim, so a second claim gives no token.
    bool const is_producer_claimed_again = pair.claim_producer().match(
        [](CellPair::ProducerClaim) noexcept { return true; }, []() noexcept { return false; });
    bool const is_consumer_claimed_again = pair.claim_consumer().match(
        [](CellPair::ConsumerClaim) noexcept { return true; }, []() noexcept { return false; });
    return is_producer_claimed_again || is_consumer_claimed_again ? 3 : 0;
}

}  // namespace

int main() {
    if (int rc = check_split(); rc != 0) return rc;
    if (int rc = check_the_ends_end_before_the_owner(); rc != 0) return 10 + rc;
    if (int rc = check_held_split(); rc != 0) return 20 + rc;
    if (int rc = check_first_states(); rc != 0) return 30 + rc;
    if (int rc = check_two_threads(); rc != 0) return 40 + rc;
    if (int rc = check_held_two_threads(); rc != 0) return 50 + rc;
    if (int rc = check_held_ends(); rc != 0) return 60 + rc;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    token_of_a_second_split_ends_the_process(test);
    foreign_token_on_a_second_thread_ends_the_process(test);
    return 0;
}
