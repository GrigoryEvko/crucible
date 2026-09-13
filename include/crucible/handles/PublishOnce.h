#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Post.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace crucible::safety {

// A contract assertion cannot carry this check. Hot-path translation
// units build with the contract semantic set to ignore, where the
// assertion elides and a second publish quietly no-ops while the first
// pointer stays visible to observers. This helper aborts regardless of
// the semantic the consuming translation unit chose.

[[noreturn]] CRUCIBLE_COLD inline void publish_once_double_publish_abort_() noexcept {
    using Tag = ::crucible::safety::diag::PublishOnceDoublePublish;
    std::fprintf(stderr,
                 "crucible: fatal: %.*s\n"
                 "  description: %.*s\n"
                 "  remediation: %.*s\n",
                 static_cast<int>(Tag::name.size()), Tag::name.data(), static_cast<int>(Tag::description.size()),
                 Tag::description.data(), static_cast<int>(Tag::remediation.size()), Tag::remediation.data());
    std::abort();
}

// This class keeps the natural alignment of an atomic pointer while
// PublishSlot below pads to a whole cache line. That slot is written
// again and again, so it must not share a line with anything. This one
// is published once per instance, so its line is invalidated once and
// read from then on. Dense structures hold thousands of these under a
// size-locked layout that padding would break. An embedder whose
// neighbouring fields are written across threads applies the alignment
// at the embed site.
template <typename T>
class CRUCIBLE_OWNER PublishOnce {
    static_assert(std::is_pointer_v<T*> || std::is_same_v<T, T>, "PublishOnce<T> is for pointer handoff — use T*");

    alignas(alignof(std::atomic<T*>)) std::atomic<T*> slot_{nullptr};

public:
    constexpr PublishOnce() noexcept = default;
    ~PublishOnce() = default;

    PublishOnce(const PublishOnce&) = delete("one publisher, one slot — copies would duplicate channel state");
    PublishOnce&
    operator=(const PublishOnce&) = delete("one publisher, one slot — copies would duplicate channel state");
    PublishOnce(PublishOnce&&) = delete("atomic is the channel identity");
    PublishOnce& operator=(PublishOnce&&) = delete("atomic is the channel identity");

    // A null pointer is the never-published state, so publishing one
    // is always a caller mistake.
    CRUCIBLE_INLINE void publish(T* ptr) noexcept pre(ptr != nullptr) {
        T* expected = nullptr;
        // The failure order is relaxed because a failure only has to
        // be detected, not synchronized with. It means another
        // publisher already claimed the slot, and the branch below
        // ends the process.
        const bool claimed =
            slot_.compare_exchange_strong(expected, ptr, std::memory_order_release, std::memory_order_relaxed);
        if (!claimed) [[unlikely]] {
            publish_once_double_publish_abort_();
        }

        // Re-reading the slot here is not a race. The exchange
        // succeeded, and any further publish from any thread fails its
        // exchange and ends the process above this line, so no writer
        // remains. The read is of this thread's own store.
        CRUCIBLE_POST(0, slot_.load(std::memory_order_relaxed) == ptr);
    }

    [[nodiscard]] CRUCIBLE_INLINE T* observe() const noexcept { return slot_.load(std::memory_order_acquire); }

    // This load is relaxed, so a true result carries no ordering. A
    // caller that goes on to dereference must reach the pointer
    // through observe.
    [[nodiscard]] CRUCIBLE_INLINE bool is_published() const noexcept {
        return slot_.load(std::memory_order_relaxed) != nullptr;
    }
};

static_assert(sizeof(PublishOnce<int>) == sizeof(std::atomic<int*>));
static_assert(sizeof(PublishOnce<void>) == sizeof(std::atomic<void*>));

template <typename T>
class CRUCIBLE_OWNER alignas(64) PublishSlot {
    std::atomic<T*> slot_{nullptr};

public:
    constexpr PublishSlot() noexcept = default;
    ~PublishSlot() = default;

    PublishSlot(const PublishSlot&) = delete("publication slot identity cannot be copied");
    PublishSlot& operator=(const PublishSlot&) = delete("publication slot identity cannot be copied");
    PublishSlot(PublishSlot&&) = delete("atomic slot is the channel identity");
    PublishSlot& operator=(PublishSlot&&) = delete("atomic slot is the channel identity");

    // This one carries no postcondition. Another publisher, or a
    // consume, can replace the pointer before the witness reads it,
    // so the witness would be racy.
    CRUCIBLE_INLINE void publish(T* ptr) noexcept pre(ptr != nullptr) { slot_.store(ptr, std::memory_order_release); }

    [[nodiscard]] CRUCIBLE_INLINE T* observe() const noexcept { return slot_.load(std::memory_order_acquire); }

    [[nodiscard]] CRUCIBLE_INLINE T* consume() noexcept { return slot_.exchange(nullptr, std::memory_order_acq_rel); }

    [[nodiscard]] CRUCIBLE_INLINE bool has_pending() const noexcept {
        return slot_.load(std::memory_order_relaxed) != nullptr;
    }
};

static_assert(alignof(PublishSlot<int>) >= 64, "PublishSlot must be cache-line aligned: repeated publish/"
                                               "exchange traffic invalidates the consumer's cached line "
                                               "every iteration, so the slot must NOT share a line with "
                                               "unrelated embedder state.");
static_assert(alignof(PublishSlot<void>) >= 64);
static_assert(sizeof(PublishSlot<int>) >= 64);
static_assert(sizeof(PublishSlot<void>) >= 64);

}  // namespace crucible::safety
