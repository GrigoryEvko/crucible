#pragma once

#include <crucible/safety/_OwnedMmap.h>

#include <type_traits>

namespace crucible::safety {

namespace detail {
template <typename T>
struct is_owned_mmap_impl : std::false_type {};

template <typename Tag, typename Prot, typename Share>
struct is_owned_mmap_impl<OwnedMmap<Tag, Prot, Share>> : std::true_type {};
}  // namespace detail

template <typename T>
inline constexpr bool is_owned_mmap_v = detail::is_owned_mmap_impl<std::remove_cv_t<T>>::value;

template <typename T>
concept IsOwnedMmap = is_owned_mmap_v<T>;

namespace self_test {
struct ProbeTag {};
struct ProbeProt {};
struct ProbeShare {};
using ProbeOwnedMmap = OwnedMmap<ProbeTag, ProbeProt, ProbeShare>;

static_assert(is_owned_mmap_v<ProbeOwnedMmap>, "OwnedMmap<...> must satisfy is_owned_mmap_v");
static_assert(is_owned_mmap_v<const ProbeOwnedMmap>, "const-qualified OwnedMmap<...> must satisfy is_owned_mmap_v");
static_assert(!is_owned_mmap_v<int>, "int must not satisfy is_owned_mmap_v");
static_assert(!is_owned_mmap_v<void*>, "raw void* must not satisfy is_owned_mmap_v");

static_assert(IsOwnedMmap<ProbeOwnedMmap>, "OwnedMmap<...> must satisfy the IsOwnedMmap concept");
static_assert(!IsOwnedMmap<int>, "int must not satisfy the IsOwnedMmap concept");

static_assert(std::is_same_v<ProbeOwnedMmap::tag_type, ProbeTag>);
static_assert(std::is_same_v<ProbeOwnedMmap::prot_type, ProbeProt>);
static_assert(std::is_same_v<ProbeOwnedMmap::share_type, ProbeShare>);
}  // namespace self_test

namespace extract {
using ::crucible::safety::is_owned_mmap_v;
using ::crucible::safety::IsOwnedMmap;
}  // namespace extract

}  // namespace crucible::safety
