// The compile-time checks of crucible/mimic/Semaphore.h.

#include <crucible/mimic/Semaphore.h>

namespace crucible::mimic {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "A signal and a wait are each one atomic operation on the counter, so the counter must be lock-free.");

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
