#pragma once

#include <crucible/Types.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

#include <cstdint>
#include <string>
#include <vector>

namespace crucible {

// Maps the hash of a source location to the location itself. Each recorded
// op carries such a hash, and after the first iteration of a loop every hash
// is already present, so the query answers yes without touching a string.
struct CallSiteTable {
    // A line number of zero is the unset marker, so the bound is at least
    // zero rather than strictly positive. A negative value can only come from
    // corrupted data.
    using Lineno = ::crucible::fixy::wrap::NonNegative<int32_t>;

    // Whatever tag a string arrived under, it is stored under this one. A
    // reader therefore cannot hand a stored string back to something that
    // wants an unvalidated one, and the two tags do not convert.
    using SanitizedName = ::crucible::fixy::wrap::Tagged<std::string, ::crucible::fixy::tags::source::Sanitized>;

    struct Entry {
        CallsiteHash hash;
        SanitizedName filename;
        SanitizedName funcname;
        Lineno lineno{int32_t{0}};
    };

    ::crucible::fixy::wrap::AppendOnly<Entry> entries;

    // An open-addressed set of the hashes seen so far. A zero hash marks an
    // empty slot.
    static constexpr uint32_t SET_CAP = 4096;
    static constexpr uint32_t SET_MASK = SET_CAP - 1;
    CallsiteHash seen[SET_CAP]{};

    [[nodiscard, gnu::hot]] bool has(CallsiteHash hash) const noexcept {
        // Rejected up front, because a query for the empty marker would
        // otherwise match the first empty slot the probe reaches.
        if (!hash) return false;
        uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
        for (uint32_t p = 0; p < SET_CAP; p++) {
            const auto& h = seen[(idx + p) & SET_MASK];
            if (h == hash) return true;
            if (h == CallsiteHash{}) return false;
        }
        return false;
    }

    // The parameter type carries the not-the-empty-marker invariant, so the
    // body below needs no check of its own.
    using NonZeroHash = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::non_zero, CallsiteHash>;

    void insert(NonZeroHash hash_nz, std::string filename, std::string funcname, int32_t lineno) {
        const CallsiteHash hash = hash_nz.value();
        if (has(hash)) return;
        uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
        for (uint32_t p = 0; p < SET_CAP; p++) {
            auto& h = seen[(idx + p) & SET_MASK];
            if (h == CallsiteHash{}) {
                h = hash;
                // This is where the line number is checked: the wrapper's
                // construction is the guard.
                entries.emplace(hash, SanitizedName{std::move(filename)}, SanitizedName{std::move(funcname)},
                                Lineno{lineno});
                return;
            }
        }
    }

    // These overloads strip the incoming tag and forward. The table only
    // ever prints these strings in diagnostics and never opens them as paths
    // or passes them to a shell, so the tag records where a string came from
    // rather than gating what may be done with it.
    using ExternalName = ::crucible::fixy::wrap::Tagged<std::string, ::crucible::fixy::tags::source::External>;
    using InternalName = ::crucible::fixy::wrap::Tagged<std::string, ::crucible::fixy::tags::source::FromInternal>;

    void insert(NonZeroHash hash_nz, ExternalName filename, ExternalName funcname, int32_t lineno) {
        insert(hash_nz, std::move(filename).into(), std::move(funcname).into(), lineno);
    }

    void insert(NonZeroHash hash_nz, InternalName filename, InternalName funcname, int32_t lineno) {
        insert(hash_nz, std::move(filename).into(), std::move(funcname).into(), lineno);
    }

    [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(entries.size()); }
};

static_assert(sizeof(CallSiteTable) >= CallSiteTable::SET_CAP * sizeof(CallsiteHash),
              "CallSiteTable footprint should be dominated by seen[]");
static_assert(sizeof(CallSiteTable) <= CallSiteTable::SET_CAP * sizeof(CallsiteHash) + 128,
              "CallSiteTable grew beyond its hash array plus a small margin");

}  // namespace crucible
