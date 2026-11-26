// The compile-time checks of crucible/CrucibleContext.h.

#include <crucible/CrucibleContext.h>

namespace crucible {

static_assert(sizeof(DispatchResult) == 8, "DispatchResult: 1+1+2+4 = 8 bytes");

static_assert(sizeof(CrucibleContext) == 208,
              "CrucibleContext: 72 engine + 24 mode, iterations and region + 24 external reads + 80 pool + 8 "
              "divergences = 208");

// A view must not outlive the frame that minted it, so storing one in a field
// would let it escape.
static_assert(::fixy::no_scoped_view_field_check<CrucibleContext>());

static_assert(sizeof(ForeignView) == 24, "ForeignView: 16 View + 4 slot + 4 pad = 24 bytes");
static_assert(sizeof(ExternalWindow) == 16, "ExternalWindow: 8 offset + 4 slot + 4 block = 16 bytes");

// The blocks of a scope have one owner.
static_assert(!std::is_copy_constructible_v<ExternalBlocks> && std::is_nothrow_move_constructible_v<ExternalBlocks>);

// The licence of with_foreign_views is one type, and no class derives from it.
struct OwnLicence final {};
static_assert(ForeignViewLicence<foreign_block_stays>);
static_assert(!ForeignViewLicence<foreign_block_stays const> && !ForeignViewLicence<foreign_block_stays&>
              && !ForeignViewLicence<OwnLicence> && !ForeignViewLicence<int>);
static_assert(std::is_final_v<foreign_block_stays>);

// The gate of the body of the compiled scope and of a scope of external slots.
using EmptyBody = decltype([](CompiledScope&) noexcept {});
using CountBody = decltype([](CompiledScope const& scope) noexcept { return scope.context().compiled_iterations(); });
using ThrowingBody = decltype([](CompiledScope&) {});
using PoolBody = decltype([](CompiledScope& scope) noexcept -> const PoolAllocator& { return scope.context().pool(); });
using AddressBody = decltype([](CompiledScope& scope) noexcept { return scope.output_ptr(OutputPosition{0}); });
struct KeyBody {
    ContextKey& key;
    void operator()(CompiledScope& /*scope*/) const noexcept {}
};
static_assert(CompiledBody<EmptyBody>);
static_assert(CompiledBody<EmptyBody&>);
static_assert(CompiledBody<CountBody>);
static_assert(!CompiledBody<ThrowingBody>, "a body of a scope must not throw past the drop of its slots");
static_assert(!CompiledBody<PoolBody>, "a result must hold no borrow of the context");
static_assert(!CompiledBody<AddressBody>, "an address of a slot leaves the replay only through a keyed door");
static_assert(!CompiledBody<KeyBody>, "a body that holds a key of a context could change the mode of a context");
static_assert(ExternalScopeBody<EmptyBody> && !ExternalScopeBody<KeyBody>);
static_assert(std::is_same_v<ExternalAnswer<EmptyBody>, ::fixy::Result<::fixy::Unit, ViewRefusal>>);
static_assert(std::is_same_v<CompiledAnswer<CountBody>, ::fixy::Option<uint64_t>>);

// The scope does not copy or move, and the key of a context costs no byte.
static_assert(!std::is_copy_constructible_v<CompiledScope> && !std::is_move_constructible_v<CompiledScope>);
static_assert(std::is_empty_v<ContextKey> && ::foundation::core::HeldKeyBrand<ContextKeyBrand>);

}  // namespace crucible
