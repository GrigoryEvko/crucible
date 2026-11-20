#pragma once

#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible {

// Maps the hash of a source location to the location itself. Each recorded
// op carries such a hash, and after the first iteration of a loop every hash
// is already present, so the query answers yes without touching a name.
struct CallSiteTable {
    // A line number of zero is the unset marker, so the bound is at least
    // zero rather than strictly positive. A negative value can only come from
    // corrupted data.
    using Lineno = ::fixy::NonNegative<int32_t>;

    // The most characters that the table keeps of one name.  A file name is a
    // path, and the kernel refuses a path of 4096 bytes or more.
    static constexpr std::size_t MAX_NAME_CHARS = 4096;

    // The table keeps one copy of each name for each hash, whichever caller
    // wrote it first, so a stored name claims Interned and neither tag of its
    // caller.  Nothing here sanitizes a name, so a stored name does not claim
    // Sanitized.  No retag leaves Interned, so a reader cannot hand a stored
    // name back to something that wants an External or an internal name.  A
    // stored name is a HeapText, so no append changes it, and a view of it
    // lives as long as the entry.
    using InternedName = ::fixy::Tagged<::fixy::HeapText, ::fixy::tags::source::Interned>;

    struct Entry {
        CallsiteHash hash;
        InternedName filename;
        InternedName funcname;
        Lineno lineno = ::fixy::mint_refined_constant<::fixy::non_negative, int32_t{0}>();
    };

    ::fixy::AppendOnly<Entry> entries = ::fixy::mint_append_only<Entry>();

    // An open-addressed set of the hashes seen so far. A zero hash marks an
    // empty slot.
    static constexpr uint32_t SET_CAP = 4096;
    static constexpr uint32_t SET_MASK = SET_CAP - 1;
    ::fixy::FixedArray<CallsiteHash, SET_CAP> seen{};

    [[nodiscard, gnu::hot]] bool has(CallsiteHash hash) const noexcept {
        // Rejected up front, because a query for the empty marker would
        // otherwise match the first empty slot the probe reaches.
        if (!hash) return false;
        const uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
        for (uint32_t p = 0; p < SET_CAP; p++) {
            const CallsiteHash& h = seen.at(slot_of_(idx + p));
            if (h == hash) return true;
            if (h == CallsiteHash{}) return false;
        }
        return false;
    }

    // The parameter type carries the not-the-empty-marker invariant, so the
    // body below needs no check of its own.
    using NonZeroHash = ::fixy::NonZero<CallsiteHash>;

    // True when the table holds the hash after the call.  A hash that the
    // table holds keeps its first names.  A name of more than MAX_NAME_CHARS
    // characters, or a full set, leaves the table as it was and gives false.
    // The allocation capability pays for the copies of the two names.
    [[nodiscard]] bool insert(::foundation::effects::Alloc alloc, NonZeroHash hash_nz, ::fixy::TextView filename,
                              ::fixy::TextView funcname, int32_t lineno) {
        const CallsiteHash hash = hash_nz.value();
        if (has(hash)) return true;
        ::fixy::GrowText stored_filename = ::fixy::mint_grow_text(MAX_NAME_CHARS);
        ::fixy::GrowText stored_funcname = ::fixy::mint_grow_text(MAX_NAME_CHARS);
        if (stored_filename.append(alloc, filename).is_err() || stored_funcname.append(alloc, funcname).is_err()) {
            return false;
        }
        const uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
        for (uint32_t p = 0; p < SET_CAP; p++) {
            CallsiteHash& h = seen.at(slot_of_(idx + p));
            if (h == CallsiteHash{}) {
                h = hash;
                // This is where the line number is checked: the checked mint
                // of the wrapper is the guard.
                entries.emplace(
                    hash,
                    ::fixy::mint_tagged<::fixy::tags::source::Interned>(::fixy::freeze(std::move(stored_filename))),
                    ::fixy::mint_tagged<::fixy::tags::source::Interned>(::fixy::freeze(std::move(stored_funcname))),
                    ::fixy::mint_refined<::fixy::non_negative>(lineno));
                return true;
            }
        }
        return false;
    }

    // These overloads strip the incoming tag and forward. The table only
    // ever prints these names in diagnostics and never opens them as paths
    // or passes them to a shell, so the tag records where a name came from
    // rather than gating what may be done with it.
    using ExternalName = ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::External>;
    using InternalName = ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::FromInternal>;

    [[nodiscard]] bool insert(::foundation::effects::Alloc alloc, NonZeroHash hash_nz, ExternalName filename,
                              ExternalName funcname, int32_t lineno) {
        return insert(alloc, hash_nz, filename.value(), funcname.value(), lineno);
    }

    [[nodiscard]] bool insert(::foundation::effects::Alloc alloc, NonZeroHash hash_nz, InternalName filename,
                              InternalName funcname, int32_t lineno) {
        return insert(alloc, hash_nz, filename.value(), funcname.value(), lineno);
    }

    [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(entries.size()); }

private:
    // The slot of a probe position.  The mask keeps the slot below SET_CAP,
    // so the optimizer removes the check.
    [[nodiscard]] static ::fixy::Index<SET_CAP> slot_of_(uint32_t position) noexcept {
        return ::fixy::Index<SET_CAP>::check(position & SET_MASK).expect("the mask keeps a slot below SET_CAP");
    }
};

}  // namespace crucible
