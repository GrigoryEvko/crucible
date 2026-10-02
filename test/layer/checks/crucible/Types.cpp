// The compile-time checks of crucible/Types.h.

#include <crucible/Types.h>

#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

namespace crucible {

static_assert(sizeof(ElementBytes) == sizeof(uint8_t), "ElementBytes must be layout-identical to uint8_t");

namespace strong_word_walk {

// The public member function of the class with that name, or no reflection.
[[nodiscard]] consteval std::meta::info public_function_(std::meta::info type, std::string_view name) {
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::current())) {
        if (std::meta::is_function(member) && std::meta::has_identifier(member)
            && std::meta::identifier_of(member) == name) {
            return member;
        }
    }
    return {};
}

// A class of crucible/Types.h that CRUCIBLE_STRONG_ID or CRUCIBLE_STRONG_HASH
// writes: it builds from its word with from_raw and gives the word back with
// raw.  The walk reads the source file of each class, so a class that another
// header declares in this namespace does not count.
[[nodiscard]] consteval bool is_strong_word_(std::meta::info member) {
    if (!std::meta::is_type(member) || !std::meta::is_class_type(member) || !std::meta::is_complete_type(member)) {
        return false;
    }
    if (!std::string_view{std::meta::source_location_of(member).file_name()}.ends_with("crucible/Types.h")) {
        return false;
    }
    return public_function_(member, "from_raw") != std::meta::info{}
        && public_function_(member, "raw") != std::meta::info{};
}

// Each identifier and each hash of the header is exactly its word, so a
// stored array of them has no padding, and the Arena can copy one as bytes.
// The fault names the first class that fails, and it is empty when each
// class holds.  A walk that finds no class is a fault, so a change of the
// macros cannot empty the check.
[[nodiscard]] consteval std::string_view strong_word_fault_() {
    std::size_t found = 0;
    for (const std::meta::info member : std::meta::members_of(^^::crucible, std::meta::access_context::current())) {
        if (!is_strong_word_(member)) {
            continue;
        }
        ++found;
        const std::string name{std::meta::display_string_of(member)};
        const std::meta::info word = std::meta::return_type_of(public_function_(member, "raw"));
        if (std::meta::size_of(member) != std::meta::size_of(word)) {
            return std::define_static_string(name + " is not the size of its word.");
        }
        if (!std::meta::is_trivially_copyable_type(member)) {
            return std::define_static_string(name + " must be trivially copyable for Arena memcpy safety.");
        }
    }
    if (found == 0) {
        return "the walk found no class of CRUCIBLE_STRONG_ID or CRUCIBLE_STRONG_HASH in crucible/Types.h.";
    }
    return {};
}

}  // namespace strong_word_walk

static_assert(strong_word_walk::strong_word_fault_().empty(), strong_word_walk::strong_word_fault_());

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(KernelCacheKey);

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
