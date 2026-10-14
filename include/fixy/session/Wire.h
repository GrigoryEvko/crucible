#pragma once

// The wire word of a choice and of a keyed step.
//
// select<I>() of a handle sends the word of branch I, and branch() reads
// the word that the peer sent.  The section on branches and labels of
// fixy/session/Fold.h gives the two kinds of word: the label word of a
// key in a keyed choice, and the position of the branch in a positional
// choice.  A branch that is no label has no word.

#include <fixy/session/Fold.h>

#include <cstddef>
#include <cstdint>
#include <meta>

namespace fixy::session::transition {

// ── The wire word of a branch ─────────────────────────────────────────

// True when each label branch of the choice names a label key.  The
// choice must be well-formed.  Complexity: linear in the branches.
[[nodiscard]] consteval bool is_keyed_choice(std::meta::info registry, const node& choice) {
    bool has_label = false;
    for (const std::meta::info branch : choice.branches) {
        const branch_head head = detail::head_of_branch(registry, branch);
        if (!head.is_label) continue;
        has_label = true;
        if (head.label_key == std::meta::info{}) return false;
    }
    return has_label;
}

// The word that picks one branch of a choice.  A branch that is no
// label has no word, and `is_wired` is false for it.
struct wire_word {
    bool is_wired = false;
    std::uint64_t value = 0;
};

// The word of branch `index` of the choice at the head of `choice`: the
// label word in a keyed choice, and the position in a positional one.
// An index past the last branch has no word.  Complexity: linear in the
// branches.
[[nodiscard]] consteval wire_word wire_word_of(std::meta::info registry, std::meta::info choice, std::size_t index) {
    const node view = decompose(registry, choice);
    if (!view.is_registered || view.entry.kind != shape_kind::choice || index >= view.branches.size()) return {};
    const branch_head head = detail::head_of_branch(registry, view.branches[index]);
    if (!head.is_label) return {};
    if (is_keyed_choice(registry, view)) return {true, head.label_word};
    return {true, static_cast<std::uint64_t>(index)};
}

// True when the choice at the head of `choice` is a keyed choice.
[[nodiscard]] consteval bool is_keyed_choice_type(std::meta::info registry, std::meta::info choice) {
    const node view = decompose(registry, choice);
    return view.is_registered && view.entry.kind == shape_kind::choice && is_keyed_choice(registry, view);
}

[[nodiscard]] consteval bool is_keyed_step_type(std::meta::info registry, std::meta::info step) {
    return is_keyed_step(registry, decompose(registry, step));
}

// The word that a keyed step sends or expects: the label word of its key.
// A step that is not keyed has no word.
[[nodiscard]] consteval wire_word wire_word_of_step(std::meta::info registry, std::meta::info step) {
    const std::meta::info key = detail::step_label_key(registry, decompose(registry, step));
    if (key == std::meta::info{}) return {};
    return {true, label_word_of(key)};
}

}  // namespace fixy::session::transition
