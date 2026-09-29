// The move of a live endpoint into a message.
//
// mint_delegated_session moves a live handle into a DelegatedSession,
// with the hold of the tokens that back its permission set.  The handle
// that the sender held is moved from, so its destructor reports no
// dropped protocol.  The record of the session stays live, because the
// session continues at the receiver.  The test accepts the delegated
// endpoint, walks the handle to End, and checks that End releases the
// record.  A consumed handle aborts at the door, as at every operation,
// and a child process proves it.  Each key of a private surface has the
// sealed shape of a passkey, so only its one door makes it.

#include <fixy/session/Delegate.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

namespace s = ::fixy::session;

struct Ping {
    int value = 0;
};

struct Wire {
    int sent = 0;
};

using Proto = s::Send<Ping, s::End>;
using Handle = decltype(s::mint_session_handle<Proto, Wire>(Wire{}));

[[nodiscard]] int fail(const char* what) {
    std::fprintf(stderr, "test_session_endpoint_transfer: %s\n", what);
    return 1;
}

// Each key is final, has no public constructor, no copy and no move, and
// is neither trivially copyable nor an implicit-lifetime type.
template <typename Key>
constexpr bool is_sealed_key =
    std::is_final_v<Key> && !std::is_default_constructible_v<Key> && !std::is_copy_constructible_v<Key>
    && !std::is_move_constructible_v<Key> && !std::is_trivially_copyable_v<Key> && !std::is_implicit_lifetime_v<Key>;
static_assert(is_sealed_key<s::HandleKey> && is_sealed_key<s::SessionOpenKey> && is_sealed_key<s::DelegationKey>);

// A handle, its core and a DelegatedSession take a key, so no expression
// without one builds them.
static_assert(!std::is_constructible_v<Handle, Wire, s::watch::session_ref, std::source_location>);
static_assert(!std::is_constructible_v<
              s::DelegatedSession<Proto, Wire, s::DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>,
              Handle, s::PermHold<::foundation::permissions::EmptyPermSet>>);

// The builders of the factory are private.
template <typename R>
concept BuildsAHandle = requires(R&& resource) {
    s::HandleFactory::make_<Proto, R, void, s::DefaultAbandonmentPolicy>(std::move(resource));
};
static_assert(!BuildsAHandle<Wire>);
static_assert(!std::is_default_constructible_v<s::HandleFactory> && !std::is_trivially_copyable_v<s::HandleFactory>);
static_assert(!std::is_default_constructible_v<s::SessionMintDoor> && std::is_final_v<s::SessionMintDoor>);
static_assert(!std::is_default_constructible_v<s::DelegationDoor> && std::is_final_v<s::DelegationDoor>);

int transfer_keeps_the_record_live() {
    const std::uint32_t live_before = s::watch::live_count();
    auto handle = s::mint_session_handle<Proto, Wire>(Wire{7});
    auto carried = s::mint_delegated_session(std::move(handle));
    if (handle.is_live()) return fail("the handle stayed live after its endpoint left it");
    if (!carried.holds_endpoint()) return fail("the payload does not hold the endpoint");
    if (s::watch::live_count() != live_before + 1) return fail("the transfer ended the session's record");

    // The receiver accepts the endpoint, and End releases the record.
    auto received = std::move(carried).accept();
    if (s::watch::live_count() != live_before + 1) return fail("the accept changed the number of live records");
    auto at_end = std::move(received).send(Ping{1}, [](Wire& wire, Ping& ping) noexcept {
        if (wire.sent != 7) return false;
        wire.sent = ping.value;
        return true;
    });
    if (s::watch::live_count() != live_before) return fail("a record stayed live after End");
    const Wire back = std::move(at_end).close();
    if (back.sent != 1) return fail("the receiver's handle did not step the transferred Resource");
    return 0;
}

// A handle that already left cannot leave again.
[[noreturn]] void take_from_a_consumed_handle() {
    auto handle = s::mint_session_handle<Proto, Wire>(Wire{});
    auto first = s::mint_delegated_session(std::move(handle));
    static_cast<void>(s::mint_delegated_session(std::move(handle)));
    static_cast<void>(first);
    std::_Exit(0);
}

int second_take_aborts() {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: the abort of a consumed handle ends the process,
    // so the attempt runs in a child that the parent observes.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: see above
    if (pid < 0) return fail("fork failed");
    if (pid == 0) take_from_a_consumed_handle();
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) return fail("waitpid failed");  // SPAWN-PROCESS-OK: see above
    if (WIFSIGNALED(status) == 0) return fail("a second transfer from one handle did not abort");
    return 0;
}

}  // namespace

int main() {
    std::fprintf(stderr, "[expected] one child process prints a use-of-a-consumed-handle diagnostic\n");
    if (const int rc = transfer_keeps_the_record_live(); rc != 0) return rc;
    if (const int rc = second_take_aborts(); rc != 0) return rc;
    return 0;
}
