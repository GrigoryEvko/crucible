// The compile-time checks of fixy/os/Pages.h.

#include <fixy/os/Pages.h>

#include <fixy/Ctx.h>

namespace fixy::pages::detail::pages_surface_invariants {

// The rows that the catalog gives to the page calls and to prctl, stated
// here as pins.
static_assert(std::is_same_v<page_call_row_t, eff::Row<eff::Effect::IO, eff::Effect::Block>>);
static_assert(std::is_same_v<thp_call_row_t, eff::Row<eff::Effect::IO, eff::Effect::Block>>);

using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsPageCall<::fixy::InitLoadCtx>);
static_assert(CtxFitsPageCall<::fixy::BgLoadCtx>);
static_assert(!CtxFitsPageCall<::fixy::HotFgCtx>, "the hot foreground context must not lock or advise pages.");
static_assert(!CtxFitsPageCall<IoOnlyCtx>,
              "a context without Block must not lock pages: a lock can fault in each page of its range.");

static_assert(CtxFitsThpCall<::fixy::InitLoadCtx>);
static_assert(!CtxFitsThpCall<::fixy::BgLoadCtx>,
              "a background context must not change the THP setting of the whole process.");
static_assert(!CtxFitsThpCall<::fixy::HotFgCtx>);

static_assert(huge_page_bytes == std::size_t{1} << 21);
static_assert(static_cast<std::uint32_t>(ThpSetting::allowed) == 0
                  && static_cast<std::uint32_t>(ThpSetting::refused) == 1,
              "the enumerators are the words of PR_GET_THP_DISABLE.");

// The storage of an object and of a run is a page range, also for an object
// with padding, which bytes() refuses.  A range holds an address word and a
// count, and no borrow: it gives out no View and converts to none, and it
// holds no padding byte.  The fixture neg_os_pages_storage_of_temporary shows
// that the storage of a temporary is refused, neg_os_page_range_to_view that
// a range gives no View, and neg_os_page_range_from_address that no range
// comes from an address.
struct PaddedRecord {
    std::uint8_t tag = 0;
    std::uint64_t value = 0;
};
inline constexpr PaddedRecord kRecord{};
static_assert(std::is_same_v<decltype(storage_of(kRecord)), PageRange>);
static_assert(std::is_same_v<decltype(storage_of(::foundation::core::View<PaddedRecord const>{})), PageRange>);
static_assert(::foundation::core::HoldsNoBorrow<PageRange>);
static_assert(!std::is_convertible_v<PageRange, ::foundation::core::View<std::byte const>>);
static_assert(!std::is_constructible_v<PageRange, ::foundation::core::View<std::byte const>>);
static_assert(sizeof(PageRange) == sizeof(PageWord) + sizeof(std::size_t));
// A range copies, and its seal refuses a copy of bytes (foundation/ByteSeal.h).
static_assert(std::is_nothrow_copy_constructible_v<PageRange> && !std::is_trivially_copyable_v<PageRange>);

}  // namespace fixy::pages::detail::pages_surface_invariants
