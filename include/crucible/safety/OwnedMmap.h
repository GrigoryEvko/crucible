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

#pragma once

#include <crucible/Platform.h>

#include <sys/mman.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Names the types that may authorize a deliberate leak.  Only a type
// whose specialization opts in satisfies it.
//
// It is a trait rather than a base class so that the authorization
// itself can be defined in the layer that gates the syscall, while
// the pivot stays here.  This wrapper never names those types, and
// the gate still fires.

template <typename G>
struct is_leak_grant : std::false_type {};

template <typename G>
inline constexpr bool is_leak_grant_v = is_leak_grant<std::remove_cv_t<std::remove_reference_t<G>>>::value;

template <typename G>
concept IsLeakGrant = is_leak_grant_v<G>;

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
    // Two gates keep this from being reached by accident.  The grant
    // parameter has to be a type that opted in, which rejects an
    // unrelated argument at overload resolution.  And the method binds
    // only to an rvalue, so releasing twice needs a second explicit
    // move; together with the sentinel swap on the way out, a double
    // unmap cannot be written.
    //
    // The grant is taken by value and is normally empty, so the
    // rationale lives in the type and costs nothing at run time.
    template <typename LeakGrant>
        requires IsLeakGrant<LeakGrant>
    [[nodiscard]] std::pair<void*, std::size_t> release(LeakGrant) && noexcept {
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

namespace self_test {
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
}  // namespace self_test

}  // namespace crucible::safety
