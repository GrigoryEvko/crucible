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

}  // namespace fixy::pages::detail::pages_surface_invariants
