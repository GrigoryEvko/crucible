// The hash of the Protocol family (foundation/core/Protocol.h) on the types of
// the product and on the shapes that a class can have: a base, a private
// member, a C array and a FixedArray.  The hash of a Guard is pinned, because
// the merkle hash of each branch folds it and the store keeps that hash.

#include <crucible/MerkleDag.h>
#include <fixy/Core.h>
#include <fixy/Protocol.h>
#include <foundation/Platform.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

struct[[= ::fixy::derive_protocol{}]] Point {
    int32_t x;
    int32_t y;
    float z;
};

// Covers the array-member case, which the fold has to walk element by
// element rather than hash whole.
struct[[= ::fixy::derive_protocol{}]] Dims {
    int64_t values[4];
    uint8_t count;
};

// The fields of Dims, with the values in a FixedArray.
struct[[= ::fixy::derive_protocol{}]] FixedDims {
    ::foundation::core::FixedArray<int64_t, 4> values{};
    uint8_t count = 0;
};

// Classes with a base.  Each base holds a field that the class does not
// repeat, so a fold that skips a base gives one hash for two values.
struct[[= ::fixy::derive_protocol{}]] LowWord {
    uint32_t low;
};
struct[[= ::fixy::derive_protocol{}]] HighWord {
    uint32_t high;
};
struct[[= ::fixy::derive_protocol{}]] WithBase : LowWord {
    uint32_t own;
};
struct[[= ::fixy::derive_protocol{}]] WithTwoBases : LowWord, HighWord {
    uint32_t own;
};
// The fields of WithBase, with the base as a member.
struct[[= ::fixy::derive_protocol{}]] WithLeadingMember {
    LowWord low;
    uint32_t own;
};
// A private base keeps its value in a private field, as a strong id does.
// The annotation consents to the walk of the private state.
class[[= ::fixy::derive_protocol{}]] PrivateWord {
public:
    constexpr explicit PrivateWord(uint32_t word) noexcept : word_{word} {}

private:
    uint32_t word_ = 0;
};
struct[[= ::fixy::derive_protocol{}]] WithPrivateBase : private PrivateWord {
    constexpr WithPrivateBase(uint32_t word, uint32_t tail) noexcept : PrivateWord{word}, own{tail} {}

    uint32_t own = 0;
};

struct[[= ::fixy::derive_protocol{}]] AvalancheSpec {
    uint64_t a, b, c, d;
};

// The raw hash of a value.
template <class T>
[[nodiscard]] uint64_t hash_of(T const& value) noexcept {
    return ::fixy::hash(value).raw();
}

int main() {
    Point p1{10, 20, 3.14f};
    Point p2{10, 20, 3.14f};
    Point p3{10, 21, 3.14f};

    uint64_t h1 = hash_of(p1);
    uint64_t h2 = hash_of(p2);
    uint64_t h3 = hash_of(p3);

    CRUCIBLE_TEST_ASSERT(kTest, h1 == h2);
    // Distinct inputs colliding is possible in principle.  The mixing
    // function makes it unlikely enough to assert against.
    CRUCIBLE_TEST_ASSERT(kTest, h1 != h3);
    CRUCIBLE_TEST_ASSERT(kTest, h1 != 0);

    Dims d1{{1, 2, 3, 4}, 4};
    Dims d2{{1, 2, 3, 4}, 4};
    Dims d3{{1, 2, 3, 5}, 4};

    CRUCIBLE_TEST_ASSERT(kTest, hash_of(d1) == hash_of(d2));
    CRUCIBLE_TEST_ASSERT(kTest, hash_of(d1) != hash_of(d3));

    // The hash of a Guard is a part of the merkle hash of each branch, and
    // the store keeps that hash.  So these values are frozen: a change of the
    // type of a field, such as the type of the pad, must keep each of them.
    // The hash is the fold of the 12 bytes of the image of the Guard in one
    // block, so a Python fold of those bytes outside the tree gives each
    // value.  CDAG_VERSION 18 pins them.
    {
        crucible::Guard by_kind{};
        by_kind.kind = crucible::Guard::Kind::OP_SEQUENCE;
        crucible::Guard by_op{};
        by_op.op_index = crucible::OpIndex{8};
        crucible::Guard by_arg{};
        by_arg.arg_index = 3;
        crucible::Guard by_dim{};
        by_dim.dim_index = 2;
        crucible::Guard by_all{};
        by_all.kind = crucible::Guard::Kind::OP_SEQUENCE;
        by_all.op_index = crucible::OpIndex{7};
        by_all.arg_index = 3;
        by_all.dim_index = 2;
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(crucible::Guard{}) == 0x4038B42334584479ULL);
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(by_kind) == 0xF13EEE04DEC598B4ULL);
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(by_op) == 0x933B5CA50B01183DULL);
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(by_arg) == 0xBA8B5554228EA232ULL);
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(by_dim) == 0x579A8AC20EC6AAA4ULL);
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(by_all) == 0x416F8BAE8D65F697ULL);
    }

    // A FixedArray field folds as its elements, with the reduction of a C
    // array field, so the two layouts give one hash.  Two values that differ
    // in one element give two hashes.
    {
        const FixedDims f1{{1, 2, 3, 4}, 4};
        const FixedDims f3{{1, 2, 3, 5}, 4};
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(f1) != hash_of(f3));
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(f1) == hash_of(d1));
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(f3) == hash_of(d3));
    }

    // Two values that differ only in a field of a base give two hashes.  The
    // bases fold first, in the order of their declarations, and then the own
    // fields.  So a value that moves from one base to the other gives a
    // different hash, and a base folds as a leading member of its type.
    {
        const WithBase low_one{{1}, 7};
        const WithBase low_two{{2}, 7};
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(low_one) != hash_of(low_two));

        const WithTwoBases high_two{{1}, {2}, 7};
        const WithTwoBases high_three{{1}, {3}, 7};
        const WithTwoBases swapped{{2}, {1}, 7};
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(high_two) != hash_of(high_three));
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(high_two) != hash_of(swapped));

        const WithPrivateBase private_one{1, 7};
        const WithPrivateBase private_two{2, 7};
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(private_one) != hash_of(private_two));

        const WithLeadingMember member_one{{1}, 7};
        CRUCIBLE_TEST_ASSERT(kTest, hash_of(low_one) == hash_of(member_one));
    }

    // The remaining cases use production types rather than fixtures, so the
    // walk is exercised over real field layouts.
    crucible::Guard g1{};
    g1.kind = crucible::Guard::Kind::SHAPE_DIM;
    g1.op_index = crucible::OpIndex{42};
    g1.arg_index = 1;
    g1.dim_index = 3;

    crucible::Guard g2 = g1;
    crucible::Guard g3 = g1;
    g3.dim_index = 4;

    CRUCIBLE_TEST_ASSERT(kTest, hash_of(g1) == hash_of(g2));
    CRUCIBLE_TEST_ASSERT(kTest, hash_of(g1) != hash_of(g3));
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::equal(g1, g2) && !::fixy::equal(g1, g3));

    // A slot of a memory plan holds strong ids, enumerations, a bool and two
    // pads, and no pointer.
    crucible::TensorSlot s1{};
    s1.nbytes = 64;
    s1.dtype = crucible::ScalarType::Float;
    s1.slot_id = crucible::SlotId{3};
    crucible::TensorSlot s2 = s1;
    crucible::TensorSlot s3 = s1;
    s3.is_external = true;
    CRUCIBLE_TEST_ASSERT(kTest, hash_of(s1) == hash_of(s2));
    CRUCIBLE_TEST_ASSERT(kTest, hash_of(s1) != hash_of(s3));

    // The gates.  A non-class type has no parts to walk, so the hash takes
    // it only as a part of a class.
    static_assert(::fixy::Encodable<Point> && ::fixy::Encodable<Dims> && ::fixy::Encodable<crucible::Guard>);
    static_assert(::fixy::Encodable<crucible::TensorSlot> && ::fixy::Decodable<crucible::TensorSlot>);
    static_assert(!::fixy::Encodable<int> && !::fixy::Encodable<float> && !::fixy::Encodable<int*>);

    // Under the strict avalanche criterion, flipping any one input bit
    // flips about half the output bits.  The mixing function has that
    // property on its own, and hashing field by field has to preserve
    // it.
    //
    // Each of the 256 input bits is flipped in turn and the number of
    // output bits that change is counted.  With a sample this small
    // the result is not a statistical test.  It does catch a gross
    // failure, such as one field never reaching part of the output.
    {
        const AvalancheSpec base{
            0xCAFEBABE12345678ULL,
            0xDEADBEEFFEEDFACEULL,
            0xABCDEF0123456789ULL,
            0x1122334455667788ULL,
        };
        const uint64_t base_h = hash_of(base);

        int total_flips = 0;
        int min_flips = 64;
        int max_flips = 0;
        for (int bit = 0; bit < 64 * 4; ++bit) {
            AvalancheSpec perturbed = base;
            const ::fixy::FixedArray<::fixy::Ref<uint64_t>, 4> fields{
                ::fixy::erase_brand(::fixy::to(perturbed.a)), ::fixy::erase_brand(::fixy::to(perturbed.b)),
                ::fixy::erase_brand(::fixy::to(perturbed.c)), ::fixy::erase_brand(::fixy::to(perturbed.d))};
            const auto bit_idx = static_cast<unsigned>(bit % 64);
            *fields.at(
                ::fixy::Index<4>::check(static_cast<std::size_t>(bit / 64)).expect("a bit of one of the fields")) ^=
                (uint64_t{1} << bit_idx);
            const uint64_t perturbed_h = hash_of(perturbed);
            const uint64_t diff = base_h ^ perturbed_h;
            const int popcount = ::fixy::popcount(diff);
            total_flips += popcount;
            if (popcount < min_flips) min_flips = popcount;
            if (popcount > max_flips) max_flips = popcount;
        }
        const double mean_flips = static_cast<double>(total_flips) / (64 * 4);
        // Half of 64 output bits is 32.  The window of six bits either
        // side absorbs the noise of a 256-sample mean.
        CRUCIBLE_TEST_ASSERT(kTest, mean_flips > 26.0 && mean_flips < 38.0);
        // A field that barely reached the output would show up as a
        // perturbation that changes almost nothing.
        CRUCIBLE_TEST_ASSERT(kTest, min_flips >= 16);
        CRUCIBLE_TEST_ASSERT(kTest, max_flips <= 50);
    }

    // A thousand distinct inputs must give a thousand distinct
    // hashes.  A chance collision at this scale is vanishingly
    // unlikely, so one here means the fold is structurally wrong
    // rather than unlucky.
    {
        constexpr int N = 1024;
        ::fixy::FixedList<uint64_t, N> seen{};
        for (int i = 0; i < N; ++i) {
            const uint64_t iu = static_cast<uint64_t>(i);
            AvalancheSpec s{
                iu,
                iu * uint64_t{0x9E3779B97F4A7C15ULL},
                iu ^ uint64_t{0xDEADBEEFCAFEBABEULL},
                ~iu,
            };
            const uint64_t hash = hash_of(s);
            const bool inserted = ::fixy::find(seen.view(), hash).is_none();
            CRUCIBLE_TEST_ASSERT(kTest, inserted && "hash collision in 1024-input grid");
            static_cast<void>(seen.append(hash).expect("the grid holds 1024 inputs"));
        }
        CRUCIBLE_TEST_ASSERT(kTest, seen.size() == N);
    }

    // The hash is pure, so a fixed input gives identical bytes on every
    // call.  A thousand calls is far more than the property needs, but it
    // would expose hidden state in a static local or a shared generator.
    {
        const Point p{42, -7, 3.14159f};
        const uint64_t h0 = hash_of(p);
        for (int i = 0; i < 1000; ++i) {
            CRUCIBLE_TEST_ASSERT(kTest, hash_of(p) == h0);
        }
    }

    // An empty edge set signs as zero and a non-empty one never does.
    // These boundary cases are the part of the contract that survives
    // any change to the bit pattern.
    {
        CRUCIBLE_TEST_ASSERT(kTest, crucible::feedback_signature({}) == 0);
        const ::foundation::core::FixedArray<crucible::FeedbackEdge, 1> one{crucible::FeedbackEdge{0, 1}};
        CRUCIBLE_TEST_ASSERT(kTest, crucible::feedback_signature(one.view()) != 0);
        // The edge count is folded in, so repeating one edge signs
        // differently from having it once.
        const ::foundation::core::FixedArray<crucible::FeedbackEdge, 2> two_same{{0, 1}, {0, 1}};
        CRUCIBLE_TEST_ASSERT(kTest,
                             crucible::feedback_signature(one.view()) != crucible::feedback_signature(two_same.view()));
        // The fold is not commutative, so reordering the edges
        // changes the signature.
        const ::foundation::core::FixedArray<crucible::FeedbackEdge, 2> ab{{1, 2}, {3, 4}};
        const ::foundation::core::FixedArray<crucible::FeedbackEdge, 2> ba{{3, 4}, {1, 2}};
        CRUCIBLE_TEST_ASSERT(kTest, crucible::feedback_signature(ab.view()) != crucible::feedback_signature(ba.view()));
    }

    // Each of the three termination fields must move the hash on its
    // own.  A fold that dropped a field, or that became commutative,
    // would still pass a test that varied them together.
    {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
            ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 12});
        ::foundation::core::with_arena_frame(arena, [alloc = test.alloc](auto& frame) noexcept {
            const auto base = crucible::mint_dag_node<crucible::LoopNode>(alloc, frame);
            base->term_kind = crucible::LoopTermKind::REPEAT;
            base->repeat_count = 100;
            base->epsilon = 0.001f;
            const uint64_t h_base = crucible::loopterm_hash(*base);

            // A loop of the arena with the three termination fields of base.
            // Each other field stays at its default, as in base.
            auto copy_of_base = [&]() noexcept {
                const auto copy = crucible::mint_dag_node<crucible::LoopNode>(alloc, frame);
                copy->term_kind = base->term_kind;
                copy->repeat_count = base->repeat_count;
                copy->epsilon = base->epsilon;
                return copy;
            };

            const auto alt_kind = copy_of_base();
            alt_kind->term_kind = crucible::LoopTermKind::UNTIL;
            CRUCIBLE_TEST_ASSERT(kTest, crucible::loopterm_hash(*alt_kind) != h_base);

            const auto alt_count = copy_of_base();
            alt_count->repeat_count = 101;
            CRUCIBLE_TEST_ASSERT(kTest, crucible::loopterm_hash(*alt_count) != h_base);

            const auto alt_eps = copy_of_base();
            alt_eps->epsilon = 0.002f;
            CRUCIBLE_TEST_ASSERT(kTest, crucible::loopterm_hash(*alt_eps) != h_base);
        });
    }

    ::fixy::check::pass(kTest, "test_reflect: all tests passed\n");
    return 0;
}
