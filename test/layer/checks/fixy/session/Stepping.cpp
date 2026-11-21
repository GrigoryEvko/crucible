// The compile-time checks of fixy/session/Stepping.h.

#include <fixy/session/Stepping.h>

#include <cstdint>

namespace fixy::session {

namespace check {

static_assert(std::is_empty_v<Off>, "check::Off must be empty, or [[no_unique_address]] cannot collapse it and a "
                                    "handle under the unchecked policy stops costing exactly its Resource.");

static_assert(!std::is_empty_v<Enforced>, "check::Enforced must carry state.  An empty Enforced would be Off wearing "
                                          "the enforcing name: every handle would report itself consumed and the "
                                          "destructor check would pass for a leaked protocol.");

static_assert(!std::is_empty_v<Cancel>, "check::Cancel must carry state.  An empty Cancel cannot tell a live handle "
                                        "from a consumed one, so it cannot know when to send the cancellation.");

static_assert(!std::is_same_v<Enforced, Cancel>, "check::Enforced and check::Cancel must be different types, "
                                                 "because the policy is part of the handle type.");

static_assert(sizeof(Enforced) == 2 * sizeof(std::source_location),
              "the record index of fixy/session/Watch.h must sit in the padding after the flag, and the policy keeps "
              "no count of steps: a handle in a loop keeps that count (fixy/session/Handle.h).");

// An owning entry point opens a tracked policy as the pinned policy of the
// same action, and keeps each other policy.
static_assert(std::is_same_v<detail::owned_policy_t<Enforced>, Pinned>,
              "with_session and a forked side must open check::Enforced as check::Pinned.");
static_assert(std::is_same_v<detail::owned_policy_t<Cancel>, PinnedCancel>,
              "with_session and a forked side must open check::Cancel as check::PinnedCancel.");
static_assert(std::is_same_v<detail::owned_policy_t<Pinned>, Pinned>
                  && std::is_same_v<detail::owned_policy_t<PinnedCancel>, PinnedCancel>
                  && std::is_same_v<detail::owned_policy_t<Off>, Off>,
              "an owning entry point must keep a pinned policy and check::Off as they are.");

}  // namespace check

static_assert(AbandonmentPolicy<check::Enforced>);
static_assert(AbandonmentPolicy<check::Cancel>);
static_assert(AbandonmentPolicy<check::Off>);

}  // namespace fixy::session
