#pragma once

// Exclusive ownership of one mmap'd region, unmapped on destruction.
//
// The empty sentinel is MAP_FAILED and not nullptr, which is what
// ::mmap returns on failure, so a caller may pass its result straight
// in and let is_mapped report.  The stored length must be exactly the
// length given to ::mmap, because ::munmap is called with it verbatim.
//
// Tag, Prot and Share are never interpreted here.  Tag gives each
// region its own type, so two unrelated mappings cannot be swapped at
// a call boundary.  Prot and Share are read by the layers that gate
// the syscall and that reason about where the region lives.
//
// The address a mapping lands at is randomized by the kernel and is
// deliberately not reproducible.  No replay path may observe it.
//
// Old spelling: include/crucible/safety/OwnedMmap.h.  The one deviation
// is the leak witness.  The old header carried its own is_leak_grant
// trait with an undefined primary, which any translation unit could
// specialize to mint an authorization out of a type of its choosing.
// The witness is now fixy::atom::IsLeakAtom, one reflection query
// over the leak atom of fixy/atoms/Os.h, so the only type that
// authorizes a leak is the one the atom catalog declares.

#include <fixy/atoms/Os.h>
#include <foundation/Platform.h>

#include <sys/mman.h>

#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace fixy {

template <typename Tag, typename Prot, typename Share>
class [[nodiscard]] OwnedMmap {
    void* addr_ = MAP_FAILED;
    std::size_t len_ = 0;

public:
    using tag_type = Tag;
    using prot_type = Prot;
    using share_type = Share;

    OwnedMmap() noexcept = default;

    explicit OwnedMmap(void* address, std::size_t length) noexcept : addr_{address}, len_{length} {}

    OwnedMmap(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");
    OwnedMmap& operator=(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap on destruction");

    OwnedMmap(OwnedMmap&& other) noexcept
        : addr_{std::exchange(other.addr_, MAP_FAILED)}, len_{std::exchange(other.len_, 0)} {}

    OwnedMmap& operator=(OwnedMmap&& other) noexcept {
        if (this != &other) {
            release_();
            addr_ = std::exchange(other.addr_, MAP_FAILED);
            len_ = std::exchange(other.len_, 0);
        }
        return *this;
    }

    ~OwnedMmap() noexcept { release_(); }

    // A borrow.  Ownership stays here, so the caller must not unmap
    // the returned pointer.
    [[nodiscard]] void* data() const noexcept { return addr_; }
    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    [[nodiscard]] bool is_mapped() const noexcept { return addr_ != MAP_FAILED && addr_ != nullptr; }

    // Hands the region to something that will unmap it on its own
    // schedule, such as a subsystem the kernel takes over.  Almost
    // every transfer of ownership is a move instead.
    //
    // Two gates keep this from being reached by accident.  The witness
    // parameter has to be a leak atom, which rejects an unrelated
    // argument at overload resolution.  And the method binds only to an
    // rvalue, so releasing twice needs a second explicit move; together
    // with the sentinel swap on the way out, a double unmap cannot be
    // written.
    //
    // The witness is taken by value and is empty, so the rationale
    // lives in the type and costs nothing at run time.
    template <atom::IsLeakAtom LeakAtom>
    [[nodiscard]] std::pair<void*, std::size_t> release(LeakAtom) && noexcept {
        return {std::exchange(addr_, MAP_FAILED), std::exchange(len_, 0)};
    }

private:
    void release_() noexcept {
        if (is_mapped()) {
            ::munmap(addr_, len_);
            addr_ = MAP_FAILED;
            len_ = 0;
        }
    }
};

namespace detail::owned_mmap_self_test {

struct DummyTag {};
struct DummyProt {};
struct DummyShare {};
using SmokeOwnedMmap = OwnedMmap<DummyTag, DummyProt, DummyShare>;

static_assert(!std::is_copy_constructible_v<SmokeOwnedMmap>, "OwnedMmap must be move-only — copy would double-unmap");
static_assert(!std::is_copy_assignable_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_move_constructible_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_move_assignable_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_default_constructible_v<SmokeOwnedMmap>);
static_assert(std::is_nothrow_destructible_v<SmokeOwnedMmap>);
static_assert(sizeof(SmokeOwnedMmap) == sizeof(void*) + sizeof(std::size_t),
              "OwnedMmap is exactly {addr, len} — no hidden padding");

// The leak witness admits the atom and nothing else.  A tag type, a
// pointer and an unrelated empty struct each fail the concept, which is
// what keeps release() from being reachable by accident.
struct NotAnAtom final {};
using SampleLeak = atom::leak::resource<atom::detail::leak_sample_rationale>;

template <typename Witness>
concept can_release = requires(SmokeOwnedMmap m, Witness w) { std::move(m).release(w); };

static_assert(can_release<SampleLeak>);
static_assert(!can_release<NotAnAtom>);
static_assert(!can_release<DummyTag>);
static_assert(!can_release<void*>);
static_assert(!can_release<int>);

}  // namespace detail::owned_mmap_self_test

}  // namespace fixy
