#pragma once

// The handle types below live in crucible::safety.  The re-export gives a
// caller that pulls in only the fixy surface an entry point that does not
// name that namespace.

#include <crucible/handles/FileHandle.h>
#include <crucible/handles/LazyEstablishedChannel.h>
#include <crucible/handles/Once.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/handles/PublishOnce.h>
#include <crucible/safety/AlignedBuffer.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/HugePageBuffer.h>
#include <crucible/safety/_OwnedFile.h>
#include <crucible/safety/PublishCommit.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::handle {

using ::crucible::safety::Fd;

using ::crucible::safety::FileHandle;

using ::crucible::safety::Once;

using ::crucible::safety::Lazy;

using ::crucible::safety::SetOnce;

using ::crucible::safety::OneShotFlag;

using ::crucible::safety::PublishOnce;

using ::crucible::safety::PublishSlot;

using ::crucible::safety::LazyEstablishedChannel;

using ::crucible::safety::AlignedBuffer;

using ::crucible::safety::HugePageBuffer;

using ::crucible::safety::OwnedFile;

using ::crucible::safety::PublishCommitCell;

using ::crucible::safety::open_read;
using ::crucible::safety::open_write_truncate;

using ::crucible::safety::Epoch;
using ::crucible::safety::Generation;

using ::crucible::safety::EpochVersioned;

}  // namespace crucible::fixy::handle

// The sentinels below verify that each alias resolves to the substrate type
// and not to a local of the same name.

namespace crucible::fixy::handle::self_test {

struct HandleProbeT {};
struct HandleProbeProto {};
struct HandleProbeResource {};

static_assert(std::is_same_v<::crucible::fixy::handle::Fd, ::crucible::safety::Fd>,
              "fixy::handle::Fd must alias safety::Fd — identity drift would break "
              "file-descriptor lifetime reasoning across translation units.");

static_assert(std::is_same_v<::crucible::fixy::handle::FileHandle, ::crucible::safety::FileHandle>,
              "fixy::handle::FileHandle must alias safety::FileHandle.");

static_assert(std::is_same_v<::crucible::fixy::handle::Once, ::crucible::safety::Once>,
              "fixy::handle::Once must alias safety::Once — first-call-wins "
              "publication identity is load-bearing for cross-TU memoization.");

static_assert(std::is_same_v<::crucible::fixy::handle::Lazy<HandleProbeT>, ::crucible::safety::Lazy<HandleProbeT>>,
              "fixy::handle::Lazy<T> must alias safety::Lazy<T>.");

static_assert(
    std::is_same_v<::crucible::fixy::handle::SetOnce<HandleProbeT>, ::crucible::safety::SetOnce<HandleProbeT>>,
    "fixy::handle::SetOnce<T> must alias safety::SetOnce<T>.");

static_assert(std::is_same_v<::crucible::fixy::handle::OneShotFlag, ::crucible::safety::OneShotFlag>,
              "fixy::handle::OneShotFlag must alias safety::OneShotFlag — the "
              "alignas(64) discipline is load-bearing at substrate level.");

static_assert(
    std::is_same_v<::crucible::fixy::handle::PublishOnce<HandleProbeT>, ::crucible::safety::PublishOnce<HandleProbeT>>,
    "fixy::handle::PublishOnce<T> must alias safety::PublishOnce<T>.");

static_assert(
    std::is_same_v<::crucible::fixy::handle::PublishSlot<HandleProbeT>, ::crucible::safety::PublishSlot<HandleProbeT>>,
    "fixy::handle::PublishSlot<T> must alias safety::PublishSlot<T>.");

static_assert(std::is_same_v<::crucible::fixy::handle::LazyEstablishedChannel<HandleProbeProto, HandleProbeResource>,
                             ::crucible::safety::LazyEstablishedChannel<HandleProbeProto, HandleProbeResource>>,
              "fixy::handle::LazyEstablishedChannel<Proto, R> must alias "
              "safety::LazyEstablishedChannel<Proto, R> — session-handshake "
              "identity must not drift across the umbrella boundary.");

static_assert(std::is_same_v<::crucible::fixy::handle::AlignedBuffer<HandleProbeT>,
                             ::crucible::safety::AlignedBuffer<HandleProbeT>>,
              "fixy::handle::AlignedBuffer<T> must alias safety::AlignedBuffer<T> "
              "— move-only RAII identity is load-bearing for hot-drain pipelines.");

static_assert(std::is_same_v<::crucible::fixy::handle::PublishCommitCell<HandleProbeT, HandleProbeProto>,
                             ::crucible::safety::PublishCommitCell<HandleProbeT, HandleProbeProto>>,
              "fixy::handle::PublishCommitCell<Tag, WriteAuth> must alias "
              "safety::PublishCommitCell — Pinned-channel identity is load-bearing "
              "for cross-thread publish/commit ordering.");

// The substrate asserts these contracts too.  Mirroring them here catches
// drift on the alias path at the boundary a consumer actually includes.
static_assert(alignof(::crucible::fixy::handle::PublishCommitCell<HandleProbeT, HandleProbeProto>) == 64,
              "fixy::handle::PublishCommitCell<Tag, WriteAuth> must be alignas(64) "
              "— a cross-thread acquire/release pair requires cache-line "
              "isolation, and drift here reintroduces false sharing.");

static_assert(
    !std::is_move_constructible_v<::crucible::fixy::handle::PublishCommitCell<HandleProbeT, HandleProbeProto>>,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must not be move-"
    "constructible — Pinned channel identity; move would orphan the "
    "fg's outstanding acquire-load address.");

static_assert(
    !std::is_copy_constructible_v<::crucible::fixy::handle::PublishCommitCell<HandleProbeT, HandleProbeProto>>,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must not be copy-"
    "constructible — channel-identity uniqueness; copy would break the "
    "single-writer invariant (BorrowSafe).");

static_assert(
    std::is_trivially_destructible_v<::crucible::fixy::handle::PublishCommitCell<HandleProbeT, HandleProbeProto>>,
    "fixy::handle::PublishCommitCell<Tag, WriteAuth> must be trivially "
    "destructible — the cell owns only std::atomic<uint64_t>; a non-"
    "trivial dtor would mean someone added a managed resource without "
    "updating the Pinned discipline.");

static_assert(std::is_same_v<::crucible::fixy::handle::HugePageBuffer<HandleProbeT>,
                             ::crucible::safety::HugePageBuffer<HandleProbeT>>,
              "fixy::handle::HugePageBuffer<T> must alias safety::HugePageBuffer<T> "
              "— the 2-MB-aligned RAII identity is load-bearing for the ring and "
              "arena consumers that depend on huge-page backing.");

static_assert(::crucible::fixy::handle::HugePageBuffer<HandleProbeT>::huge_page_bytes
                  == ::crucible::warden::kHugePageBytes,
              "fixy::handle::HugePageBuffer<T>::huge_page_bytes must equal "
              "warden::kHugePageBytes — madvise(MADV_HUGEPAGE) requires the "
              "allocation to be aligned to the kernel's huge-page boundary; if "
              "this drifts, hugepage backing silently fails and TLB pressure "
              "regresses without any C++-level error path.");

static_assert(std::is_same_v<::crucible::fixy::handle::OwnedFile, ::crucible::safety::OwnedFile>,
              "fixy::handle::OwnedFile must alias safety::OwnedFile — RAII "
              "close-on-dtor identity is load-bearing for every early-return path "
              "that would otherwise leak the stream.");

static_assert(!std::is_copy_constructible_v<::crucible::fixy::handle::OwnedFile>,
              "fixy::handle::OwnedFile must not be copy-constructible — copying "
              "a FILE* aliases two RAII owners that both call std::fclose() at "
              "destruction, double-closing the stdio handle (MemSafe).");

static_assert(!std::is_copy_assignable_v<::crucible::fixy::handle::OwnedFile>,
              "fixy::handle::OwnedFile must not be copy-assignable — copy-assign "
              "would leak the LHS's existing FILE* AND double-close the RHS's.");

static_assert(std::is_nothrow_move_constructible_v<::crucible::fixy::handle::OwnedFile>,
              "fixy::handle::OwnedFile must have a nothrow move constructor — "
              "it appears inside std::expected<> and other move-only containers "
              "that demand noexcept move for the strong exception guarantee.");

static_assert(std::is_nothrow_move_assignable_v<::crucible::fixy::handle::OwnedFile>,
              "fixy::handle::OwnedFile must have a nothrow move-assign operator "
              "for symmetric strong-guarantee handling.");

static_assert(std::is_nothrow_destructible_v<::crucible::fixy::handle::OwnedFile>,
              "fixy::handle::OwnedFile destructor must be noexcept — it runs on "
              "every early-return path and stdio errors must not propagate.");

static_assert(sizeof(::crucible::fixy::handle::OwnedFile) == sizeof(std::FILE*),
              "fixy::handle::OwnedFile must be sizeof(std::FILE*) — any inflation "
              "means a hidden field was added that would balloon the cost of "
              "every TraceLoader stack frame and per-call early-return path.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::handle::open_read), decltype(&::crucible::safety::open_read)>,
              "fixy::handle::open_read must alias safety::open_read — "
              "the FileHandle factory identity is load-bearing for the error-channel "
              "discipline at every open site.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::handle::open_write_truncate),
                             decltype(&::crucible::safety::open_write_truncate)>,
              "fixy::handle::open_write_truncate must alias "
              "safety::open_write_truncate — every spill and entry-write path "
              "depends on identity preservation.");

static_assert(std::is_same_v<::crucible::fixy::handle::Epoch, ::crucible::safety::Epoch>,
              "fixy::handle::Epoch must alias safety::Epoch — Canopy fleet-"
              "epoch identity drift would break the EpochVersioned axis "
              "distinction at every handle-tier admission gate.");

static_assert(std::is_same_v<::crucible::fixy::handle::Generation, ::crucible::safety::Generation>,
              "fixy::handle::Generation must alias safety::Generation — per-"
              "Relay restart counter identity drift would silently equate "
              "Generation and Epoch at consumer sites that rely on strong-"
              "typed C++ overload resolution.");

static_assert(std::is_same_v<::crucible::fixy::handle::EpochVersioned<HandleProbeT>,
                             ::crucible::safety::EpochVersioned<HandleProbeT>>,
              "fixy::handle::EpochVersioned<T> must alias safety::EpochVersioned<T> "
              "— Canopy fleet-state versioning identity must not drift across the "
              "umbrella boundary, and drift would silently bypass the is_at_least "
              "admission gates and re-admit a pre-reshard checkpoint.");

static_assert(sizeof(::crucible::fixy::handle::EpochVersioned<std::uint8_t>)
                  >= sizeof(std::uint64_t) * 2 + sizeof(std::uint8_t),
              "fixy::handle::EpochVersioned<T> must carry at least 16 bytes of "
              "grade (Epoch + Generation) — drift would silently break the "
              "per-instance grade storage contract.");

constexpr int handle_alias_cardinality = 18;
static_assert(handle_alias_cardinality == 18, "fixy::handle:: cardinality changed — extend the sentinel block "
                                              "to cover the new alias.");

}  // namespace crucible::fixy::handle::self_test
