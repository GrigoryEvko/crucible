#pragma once

// A device semaphore is a 64-bit counter that one agent signals and another
// agent polls.  The caller owns the counter, and the descriptor borrows it.
//
// No vendor backend exists at this time.  Each backend uses the host oracle:
// a signal is a release store of the value, and a wait is one acquire load
// that compares the counter with the expected value.  When a vendor backend
// exists, it replaces the oracle for its own Backend.

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/reflect/EnumName.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::mimic {

using ::foundation::algebra::lattices::VendorBackend;
using ::foundation::algebra::lattices::VendorLattice;

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "A signal and a wait are each one atomic operation on the counter, so the counter must be lock-free.");

// A backend that names one device family.  None names no kernel, and Portable
// names no one device, so a semaphore on either has no device to signal.  The
// concept also refuses a value that no enumerator holds.
template <VendorBackend Backend>
concept SemaphoreBackend = !::foundation::reflect::enumerator_name(Backend).empty()
                        && Backend != VendorLattice::bottom() && Backend != VendorLattice::top();

template <VendorBackend Backend>
    requires SemaphoreBackend<Backend>
class DeviceSemaphore;

// The one door to a DeviceSemaphore.  It is declared here, so that the class
// can name it as the friend of its only constructor.  It is defined after the
// class.
template <VendorBackend Backend>
    requires SemaphoreBackend<Backend>
[[nodiscard]] constexpr DeviceSemaphore<Backend>
mint_device_semaphore(std::atomic<std::uint64_t>& counter CRUCIBLE_LIFETIMEBOUND, std::uint64_t native_handle) noexcept;

// The descriptor borrows its counter, so a temporary counter dangles at the
// end of the full expression.  This form is the better match for a
// temporary, and it is deleted.
template <VendorBackend Backend>
    requires SemaphoreBackend<Backend>
constexpr DeviceSemaphore<Backend> mint_device_semaphore(std::atomic<std::uint64_t>&& counter,
                                                         std::uint64_t native_handle) noexcept =
    delete("The DeviceSemaphore borrows its counter, so a temporary counter dangles at the end of the full "
           "expression. Bind the counter to a name first.");

// The constructor is private, and mint_device_semaphore is its one friend.
// There is no default constructor, so each descriptor points to a counter.
//
// The two assignments are user-provided, so the class is not trivially
// copyable, and std::bit_cast cannot build one from bytes.  The annotation
// refuses a lifetime start over bytes.  The copy and move constructors stay
// trivial, so a descriptor goes in registers.
template <VendorBackend Backend>
    requires SemaphoreBackend<Backend>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] DeviceSemaphore {
public:
    static constexpr VendorBackend backend = Backend;

    DeviceSemaphore() = delete("A DeviceSemaphore borrows a counter. Take one from mint_device_semaphore.");
    constexpr DeviceSemaphore(const DeviceSemaphore&) noexcept = default;
    constexpr DeviceSemaphore(DeviceSemaphore&&) noexcept = default;

    constexpr DeviceSemaphore& operator=(const DeviceSemaphore& other) noexcept {
        counter_ = other.counter_;
        native_handle_ = other.native_handle_;
        return *this;
    }
    constexpr DeviceSemaphore& operator=(DeviceSemaphore&& other) noexcept {
        counter_ = other.counter_;
        native_handle_ = other.native_handle_;
        return *this;
    }

    [[nodiscard]] constexpr std::uint64_t native_handle() const noexcept { return native_handle_; }

    // A waiter that reads the value with an acquire load sees each write that
    // the signaler made before the signal.
    void signal(std::uint64_t value) const noexcept { counter_->store(value, std::memory_order_release); }

    // One acquire load.  It never spins, yields or blocks, so the caller
    // selects the retry policy.  A true result says that the counter had
    // the expected value, or a larger one, at the time of the load.
    [[nodiscard]] bool try_wait(std::uint64_t expected) const noexcept {
        return counter_->load(std::memory_order_acquire) >= expected;
    }

private:
    constexpr DeviceSemaphore(std::atomic<std::uint64_t>& counter, std::uint64_t native_handle) noexcept
        : counter_{&counter}, native_handle_{native_handle} {}

    template <VendorBackend B>
        requires SemaphoreBackend<B>
    friend constexpr DeviceSemaphore<B> mint_device_semaphore(std::atomic<std::uint64_t>& counter,
                                                              std::uint64_t native_handle) noexcept;

    std::atomic<std::uint64_t>* counter_ = nullptr;
    std::uint64_t native_handle_ = 0;
};

template <VendorBackend Backend>
    requires SemaphoreBackend<Backend>
[[nodiscard]] constexpr DeviceSemaphore<Backend> mint_device_semaphore(std::atomic<std::uint64_t>& counter,
                                                                       std::uint64_t native_handle) noexcept {
    return DeviceSemaphore<Backend>{counter, native_handle};
}

namespace detail::device_semaphore_self_test {

// A descriptor comes only from the mint: there is no default constructor, no
// constructor that a caller can reach, no aggregate form and no build from
// bytes.  A minted descriptor still copies, and its destruction is trivial,
// because it owns nothing.
template <VendorBackend Backend>
inline constexpr bool descriptor_is_closed_v =
    DeviceSemaphore<Backend>::backend == Backend
    && !std::is_default_constructible_v<DeviceSemaphore<Backend>> && !std::is_aggregate_v<DeviceSemaphore<Backend>>
    && !std::is_constructible_v<DeviceSemaphore<Backend>, std::atomic<std::uint64_t>&, std::uint64_t>
    && !std::is_trivially_copyable_v<DeviceSemaphore<Backend>>
    && !::foundation::lifetime::ImplicitLifetimeThroughout<DeviceSemaphore<Backend>>
    && std::is_nothrow_copy_constructible_v<DeviceSemaphore<Backend>>
    && std::is_trivially_destructible_v<DeviceSemaphore<Backend>>;

// The walk reads each enumerator of VendorBackend, so a new device family
// gets its descriptor, and the walk checks it.  Complexity: linear in the
// number of enumerators.
[[nodiscard]] consteval bool every_descriptor_is_closed() {
    static constexpr auto backends =
        std::define_static_array(std::meta::enumerators_of(^^::foundation::algebra::lattices::VendorBackend));
    std::size_t descriptor_count = 0;
// An expansion statement unrolls into successive scopes that each declare
// the same induction variable, so -Wshadow fires once per iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto enumerator : backends) {
        constexpr VendorBackend B = [:enumerator:];
        if constexpr (SemaphoreBackend<B>) {
            if (!descriptor_is_closed_v<B>) return false;
            ++descriptor_count;
        }
    }
#pragma GCC diagnostic pop
    return descriptor_count != 0;
}
static_assert(every_descriptor_is_closed(), "A DeviceSemaphore has a door other than mint_device_semaphore.");

static_assert(SemaphoreBackend<VendorBackend::CPU> && SemaphoreBackend<VendorBackend::NV>
              && SemaphoreBackend<VendorBackend::AMD> && SemaphoreBackend<VendorBackend::TPU>
              && SemaphoreBackend<VendorBackend::TRN> && SemaphoreBackend<VendorBackend::CER>);
static_assert(!SemaphoreBackend<VendorBackend::None>);
static_assert(!SemaphoreBackend<VendorBackend::Portable>);
static_assert(!SemaphoreBackend<static_cast<VendorBackend>(7)>);
static_assert(sizeof(DeviceSemaphore<VendorBackend::CPU>) == sizeof(void*) + sizeof(std::uint64_t));

}  // namespace detail::device_semaphore_self_test

}  // namespace crucible::mimic
