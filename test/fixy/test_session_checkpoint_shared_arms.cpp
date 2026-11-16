// The checkpoint check of fixy/session/Checkpoint.h on a protocol with shared
// arms.
//
// Each pair below is a row of 40 choices.  Each choice has 32 labels, and
// each label leads to the next choice.  The model of the pair has 41
// configurations, and the protocol tree has 32 to the power 40 nodes.  The
// check of the shape keeps one answer for each node, so the row is
// compliant.  A check that visits the protocol as a tree stops at the bound
// of the work and gives TooManyConfigurations.
//
// A constant evaluation builds each pair by substitution, and no declaration
// of the unit names a protocol of a pair.  GCC writes the debug name of a
// type that a declaration names, with each template argument in full.  The
// name of a row of shared arms has 32 to the power 40 parts, so such a
// declaration makes the compile of the unit with -g never end.  The rules
// step-cost and step-memory judge the compile of this unit, so a check that
// spells the protocol of a pair stops the gate.

#include <fixy/session/Checkpoint.h>

#include <meta>

namespace s = ::fixy::session;
namespace tr = ::fixy::session::transition;

namespace {

inline constexpr int kRowLength = 40;
inline constexpr int kLabels = 32;

// A row of `steps` choices of the family `choice`.  Each choice has `labels`
// branches, and each branch is the next choice.  The last choice leads to
// `last`.  Complexity: steps times labels.
consteval std::meta::info row_of_choices(std::meta::info choice, std::meta::info last, int steps, int labels) {
    std::meta::info next = last;
    for (int step = 0; step < steps; ++step) {
        tr::stack<std::meta::info> branches{};
        for (int label = 0; label < labels; ++label)
            tr::push(branches, next);
        next = std::meta::substitute(choice, branches);
    }
    return next;
}

// The row ends at End on the two sides, so each label exchange reaches the
// next pair of choices and the last one reaches End against End.
static_assert(s::checkpoint_verdict_of(row_of_choices(^^s::Select, ^^s::End, kRowLength, kLabels),
                                       row_of_choices(^^s::Offer, ^^s::End, kRowLength, kLabels))
              == s::CheckpointVerdict::Compliant);

// The check keeps an answer for a node at one place.  Roll is legal as a
// branch of a choice and not after a send, so the answer of the first Roll
// does not answer the second one.
static_assert(s::checkpoint_verdict_of(^^s::Select<s::Roll, s::Send<int, s::Roll>>,
                                       ^^s::Offer<s::Roll, s::Recv<int, s::Roll>>)
              == s::CheckpointVerdict::NotCheckpointShaped);

}  // namespace

int main() { return 0; }
