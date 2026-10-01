// The compile-time checks of crucible/Types.h.

#include <crucible/Types.h>

namespace crucible {

static_assert(sizeof(ElementBytes) == sizeof(uint8_t), "ElementBytes must be layout-identical to uint8_t");

// A new hash type with no family specialisation fails to instantiate these,
// and a hash that changes family fails the one that names it.
static_assert(IsFamilyA<SchemaHash>, "SchemaHash must be persistent: an operation's identity is compared "
                                     "across processes.");
static_assert(IsFamilyA<ShapeHash>, "ShapeHash must be persistent: tensor geometry is compared bit for bit "
                                    "across a replay.");
static_assert(IsFamilyA<ScopeHash>, "ScopeHash must be persistent: a module path identity is stored.");
static_assert(IsFamilyA<CallsiteHash>, "CallsiteHash must be persistent: a source-location identity is "
                                       "pinned in stored expectations.");
static_assert(IsFamilyA<ContentHash>, "ContentHash must be persistent: a region's structural identity keys "
                                      "stored objects and compiled kernels.");
static_assert(IsFamilyA<MerkleHash>, "MerkleHash must be persistent: a subtree identity is stored and "
                                     "compared across vendors.");
static_assert(IsFamilyA<RecipeHash>, "RecipeHash must be persistent: a numerical recipe's identity is "
                                     "shared between installations.");
static_assert(IsFamilyA<RowHash>, "RowHash must be persistent: an effect row's identity is half of a "
                                  "compiled-kernel lookup key.");

// The two families are disjoint.
static_assert(!IsFamilyB<ContentHash>, "a persistent hash must not also be process-local: the separation "
                                       "of the two families is what makes either safe.");

static_assert(sizeof(KernelCacheKey) == 16, "KernelCacheKey must be exactly two 64-bit hashes with no padding.");
static_assert(alignof(KernelCacheKey) == 8, "KernelCacheKey must be 8-byte aligned to stay compatible with a "
                                            "paired atomic on the supported architectures.");

}  // namespace crucible
