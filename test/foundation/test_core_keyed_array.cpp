// Tests of foundation/core/KeyedArray.h: the holders of a HeapArray and of a
// MaskedArray that lend one element in place only with the LoanKey of their
// brand.

#include <foundation/core/KeyedArray.h>

#include <foundation/core/Array.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace core = ::foundation::core;

// The count of live Counted objects.  A holder must end each element one time
// and leak none.
int live_counted = 0;

class Counted {
public:
    explicit Counted(int label) noexcept : label_{label} { ++live_counted; }
    Counted(Counted const& other) noexcept : label_{other.label_} { ++live_counted; }
    Counted& operator=(Counted const& other) noexcept {
        label_ = other.label_;
        return *this;
    }
    ~Counted() { --live_counted; }

    [[nodiscard]] int label() const noexcept { return label_; }

private:
    int label_ = 0;
};

// A class that holds a keyed table and the key of its brand beside it, as the
// state of the trace build does.  The proof of the brand has a private
// constructor, and the holder is its one friend, so only the holder mints a key
// of the brand.
class TableHolder {
public:
    struct TableBrand final {
        class key_proof final {
            friend class TableHolder;

            constexpr key_proof() noexcept {}

        public:
            key_proof(key_proof const&) = delete("a proof of a held key stays with its holder");
            key_proof& operator=(key_proof const&) = delete("a proof of a held key stays with its holder");
        };
    };

    // The table takes count new slots, each zero.
    void grow(std::size_t count) noexcept {
        table_.replace(key_,
                       core::mint_masked_array<std::uint64_t>(::foundation::effects::testing::test().alloc, count));
    }

    // Adds value to the slot of key, in place, and gives the sum.
    [[nodiscard]] std::uint64_t add(std::uint64_t key, std::uint64_t value) noexcept {
        return table_.with_masked(key_, key, [value](std::uint64_t& slot) noexcept {
            slot += value;
            return slot;
        });
    }

    void clear() noexcept { table_.fill(key_, 0); }

    [[nodiscard]] core::KeyedMaskedArray<std::uint64_t, TableBrand> const& table() const noexcept { return table_; }

private:
    core::KeyedMaskedArray<std::uint64_t, TableBrand> table_{};
    [[no_unique_address]] core::LoanKey<TableBrand> key_{core::mint_held_key<TableBrand>(TableBrand::key_proof{})};
};

// A holder of a held key grows its table, writes a slot in place, and clears
// the table, each with its own key.  The key takes no byte.
void test_held_key() {
    static_assert(sizeof(TableHolder) == sizeof(core::MaskedArray<std::uint64_t>));
    TableHolder holder{};
    CRUCIBLE_FATAL_INVARIANT(holder.table().size() == 0);
    holder.grow(8);
    CRUCIBLE_FATAL_INVARIANT(holder.table().size() == 8);
    CRUCIBLE_FATAL_INVARIANT(holder.add(11, 5) == 5 && holder.add(3, 2) == 7);
    CRUCIBLE_FATAL_INVARIANT(holder.table().at_masked(3) == 7 && holder.table().at_masked(4) == 0);
    holder.clear();
    CRUCIBLE_FATAL_INVARIANT(holder.table().at_masked(3) == 0 && holder.table().size() == 8);
    holder.grow(16);
    CRUCIBLE_FATAL_INVARIANT(holder.table().size() == 16 && holder.add(19, 1) == 1);
}

// The element door of a KeyedHeapArray writes the element at a position below
// the count in place, and runs the none arm at or past the count.
void test_element_door() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto [words, key] = core::mint_keyed_owner(core::keyed_heap_array_owner<std::uint64_t>,
                                               core::mint_heap_array(alloc, 4, std::uint64_t{1}));
    int const steps = words.with_element(
        key, 2,
        [](std::uint64_t& word) noexcept {
            word |= 16;
            return 1;
        },
        [] noexcept { return 0; });
    int const missed = words.with_element(
        key, 4, [](std::uint64_t& word) noexcept { return static_cast<int>(word); }, [] noexcept { return -1; });
    CRUCIBLE_FATAL_INVARIANT(steps == 1 && missed == -1);
    std::uint64_t total = 0;
    for (std::uint64_t const word : words) {
        total += word;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 20 && words.size() == 4);
    CRUCIBLE_FATAL_INVARIANT(*words.view().get(2).expect("the holder holds four words") == 17);
}

// The element door of a KeyedMaskedArray writes the element at the masked key
// in place, and fill writes each element.
void test_masked_door_and_fill() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto [table, key] = core::mint_keyed_owner(core::keyed_masked_array_owner<std::uint32_t>,
                                               core::mint_masked_array<std::uint32_t>(alloc, 8));
    table.with_masked(key, 11, [](std::uint32_t& slot) noexcept { slot = 9; });
    CRUCIBLE_FATAL_INVARIANT(table.at_masked(3) == 9 && table.at_masked(11) == 9 && table.at_masked(4) == 0);
    table.fill(key, 6);
    std::uint32_t total = 0;
    for (std::uint32_t const slot : table.view()) {
        total += slot;
    }
    CRUCIBLE_FATAL_INVARIANT(total == 48);
}

// replace gives the holder the elements of a fresh array and ends the old
// elements.  take gives the array back, and the holder is then empty.  The end
// of a holder ends each element that it holds.
void test_replace_and_take() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    {
        auto [items, key] =
            core::mint_keyed_owner(core::keyed_heap_array_owner<Counted>, core::mint_heap_array(alloc, 3, Counted{1}));
        CRUCIBLE_FATAL_INVARIANT(live_counted == 3);
        items.replace(key, core::mint_heap_array(alloc, 2, Counted{2}));
        CRUCIBLE_FATAL_INVARIANT(live_counted == 2 && items.size() == 2);
        CRUCIBLE_FATAL_INVARIANT(items.view().get(1).expect("the fresh array holds two items")->label() == 2);
        core::HeapArray<Counted> const given = items.take(std::move(key));
        CRUCIBLE_FATAL_INVARIANT(items.size() == 0 && given.size() == 2 && live_counted == 2);
    }
    CRUCIBLE_FATAL_INVARIANT(live_counted == 0);
    {
        auto [items, key] =
            core::mint_keyed_owner(core::keyed_heap_array_owner<Counted>, core::mint_heap_array(alloc, 4, Counted{3}));
        int const label =
            items.with_element(key, 0, [](Counted& item) noexcept { return item.label(); }, [] noexcept { return 0; });
        CRUCIBLE_FATAL_INVARIANT(label == 3 && live_counted == 4);
    }
    CRUCIBLE_FATAL_INVARIANT(live_counted == 0);
}

}  // namespace

int main() {
    test_held_key();
    test_element_door();
    test_masked_door_and_fill();
    test_replace_and_take();
    return 0;
}
