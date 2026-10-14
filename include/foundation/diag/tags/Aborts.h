#pragma once

// The tags of the run-time aborts: a full share pool, a failed huge-page
// allocation, a second publish, a flag word outside its invariant, a borrowed
// view read outside its bounds and an allocation size that wraps.  A cold
// path that prints the entry of one of these tags includes this header and
// no other domain file.  The authoring rule of a tag is in
// foundation/diag/Tag.h.

#include <foundation/diag/Tag.h>

namespace foundation::diag {

struct SharedPermissionPoolSaturated : tag_base {
    static constexpr tag_text_literal name = "SharedPermissionPoolSaturated";
    static constexpr tag_text_literal description = "SharedPermissionPool<Tag>::lend_raw_() observed an outstanding-"
                                                    "lend count at COUNT_MASK (2^63 - 1) — the atomic state word "
                                                    "cannot admit another lend without aliasing the count field "
                                                    "with the EXCLUSIVE_OUT_BIT.  Incrementing past the mask would "
                                                    "silently mark the pool as upgrade-in-progress AND wrap the "
                                                    "count to zero, producing two contradictory states at once.  "
                                                    "Reaching this site indicates either (a) a runaway borrow leak "
                                                    "(legitimate guards never destructed), (b) a pool reused across "
                                                    "an unbounded lifetime that should have been re-rooted via "
                                                    "mint_permission_root, or (c) a wraparound attack against the "
                                                    "fractional-permission accounting.";
    static constexpr tag_text_literal remediation = "Audit lifetime of every SharedPermissionGuard<Tag> sourced "
                                                    "from the pool — a leaked guard prevents the count from ever "
                                                    "decrementing.  If borrow lifetimes are correct but throughput "
                                                    "genuinely exceeds 2^63 lend operations across the pool's "
                                                    "lifetime, refactor to use a fresh pool rooted by "
                                                    "mint_permission_root<Tag>() at a higher boundary so older "
                                                    "pools can be sunk.  COUNT_MASK is structural; it is not "
                                                    "tunable because the layout shares its top bit with "
                                                    "EXCLUSIVE_OUT_BIT for lock-free upgrade signaling.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
        "SharedPermissionPool<Tag> encodes Boyland 2003 fractional "
        "permissions in a single atomic state word: low 63 bits count "
        "outstanding borrows, top bit signals exclusive-upgrade in "
        "progress.  Saturation at COUNT_MASK (2^63 - 1) means the next "
        "lend would either alias the upgrade bit (silently marking the "
        "pool busy while admitting a phantom borrower) or wrap to zero "
        "(losing every outstanding guard's ledger entry).  Either "
        "outcome breaks CSL frame discipline — the type system loses "
        "its proof that read borrows are disjoint from writes.  Pre-fix "
        "the saturation site std::abort()ed with no breadcrumb; "
        "post-fix this tag is emitted before terminating.";
    static constexpr tag_text_literal symptom_pattern =
        "Surfaces in long-lived pools after a leaked SharedPermission"
        "Guard (orphaned worker, exception during construct, missing "
        "RAII).  Reaching 2^63 lifetime lends on a single pool requires "
        "either a guard leak or genuinely unbounded reuse — the fix "
        "depends on which.";
    static constexpr tag_text_literal correct_example =
        "{ auto guard = pool.lend(); use(guard); }  // RAII drop releases";
    static constexpr tag_text_literal violating_example =
        "auto* leak = new std::optional<SharedPermissionGuard<Tag>>(pool.lend());  // never deleted";
};

struct HugePageAllocationFailed : tag_base {
    static constexpr tag_text_literal name = "HugePageAllocationFailed";
    static constexpr tag_text_literal description = "foundation::AlignedBuffer<T, huge_page_bytes>::allocate(count) "
                                                    "received a null pointer from std::aligned_alloc(huge_page_bytes, "
                                                    "bytes), where bytes is count * sizeof(T) rounded up to the "
                                                    "huge-page size.  The heap could not supply an extent of that "
                                                    "size at 2 MiB alignment.  The usual causes are an address space "
                                                    "too fragmented to hold such an extent, and a memory limit "
                                                    "(RLIMIT_AS or the memory.max of the cgroup) that the request "
                                                    "is more than.  The metadata log of the recording path lives in "
                                                    "this buffer, so the runtime cannot start without it.";
    static constexpr tag_text_literal remediation = "Examine the memory limits of the process: ulimit -v, and the "
                                                    "memory.max of its cgroup.  Increase the limit that the request "
                                                    "is more than, or decrease the capacity of the buffer.  The "
                                                    "alignment does not ask the kernel for huge pages, so the "
                                                    "huge-page pool (/proc/sys/vm/nr_hugepages) has no effect on "
                                                    "this failure.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters = "The metadata log of the recording path lives in this "
                                                         "buffer.  The runtime cannot record without it, so a failed "
                                                         "allocation stops the process while it starts.  The 2 MiB "
                                                         "alignment is necessary for the kernel to back the region "
                                                         "with huge pages, and it is not sufficient: the advice that "
                                                         "asks for them is a separate step.";
    static constexpr tag_text_literal symptom_pattern =
        "An abort while the first Vigil is constructed, on a host or in a "
        "container whose memory limit is less than the capacity of the "
        "metadata log.";
    static constexpr tag_text_literal correct_example =
        "// Increase memory.max of the container, or decrease the capacity of the log";
    static constexpr tag_text_literal violating_example =
        "auto log = AlignedBuffer<TensorMeta, huge_page_bytes>::allocate(1u << 20);  // memory.max is 128M";
};

struct PublishOnceDoublePublish : tag_base {
    static constexpr tag_text_literal name = "PublishOnceDoublePublish";
    static constexpr tag_text_literal description = "fixy::handle::PublishOnce<T>::publish found the slot already "
                                                    "published.  PublishOnce takes one publisher over the full life "
                                                    "of the slot, and any number of observers.  The exchange from "
                                                    "null to the new pointer failed, so a different publisher came "
                                                    "first.  publish then prints this entry and ends the process, "
                                                    "under each contract semantic.";
    static constexpr tag_text_literal remediation = "Find the second path that reaches publish.  One slot takes at "
                                                    "most one publish call.  The usual causes are two threads that "
                                                    "both establish one channel, a retry loop that calls publish "
                                                    "again after an error, and a refactored call site that adds a "
                                                    "second publisher.  When several threads can publish, call "
                                                    "publish inside fixy::handle::Once::call.  The first thread "
                                                    "publishes, and the other threads wait and do not publish.  "
                                                    "fixy::channels::LazyEstablishedChannel does not serialize "
                                                    "establish: establish calls publish directly, so a second "
                                                    "establish also ends the process.  After establish, the channel "
                                                    "gives its one session to the first observer, and it refuses "
                                                    "each later request with AlreadyClaimed.  When the slot holds a "
                                                    "federation cache entry, a second publish shows a key collision "
                                                    "or two compiles of one entry.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
        "Observers read the published pointer with an acquire load and "
        "keep it.  If a second publish could replace the pointer, an "
        "observer could hold a pointer that the slot no longer "
        "publishes, and two observers could disagree about the channel.  "
        "A contract assertion cannot carry this check, because hot-path "
        "translation units build with the contract semantic set to "
        "ignore, and the assertion then does nothing.  publish calls an "
        "abort helper instead, and the helper runs under each semantic.";
    static constexpr tag_text_literal symptom_pattern =
        "Two threads that each establish one channel, with no Once around "
        "the call.  Or a retry loop that calls publish again after an "
        "error and does not return the error.  When PublishOnce holds a "
        "federation cache entry, the entry fires on a key collision or "
        "on two compiles of one entry.";
    static constexpr tag_text_literal correct_example =
        "once.call([&]() noexcept { slot.publish(p); });  // one publisher";
    static constexpr tag_text_literal violating_example =
        "if (need_publish) slot.publish(p);  // a second thread can publish too";
};

struct BitsInvariantViolation : tag_base {
    static constexpr tag_text_literal name = "BitsInvariantViolation";
    static constexpr tag_text_literal description = "A fixy::Bits<EnumType> word holds a value outside the "
                                                    "invariant of its flag enum.  fixy::Bits does not check that "
                                                    "invariant: from_raw admits every value of the underlying type, "
                                                    "and set() admits two flags that exclude each other.  The code "
                                                    "that validates the word names this tag.  Three concrete "
                                                    "failure modes route through this tag: (a) Bits<E>::from_raw(b) "
                                                    "loaded a deserialized bit-pattern containing flags outside "
                                                    "E's declared mask (e.g., the on-disk word survived an enum-"
                                                    "tightening upgrade and now carries a bit no current E "
                                                    "enumerator names); (b) a mutual-exclusion invariant "
                                                    "observed two mutually-"
                                                    "exclusive flags set simultaneously (e.g., NodeFlags::Dirty + "
                                                    "NodeFlags::Sealed); (c) a subsumption invariant observed a "
                                                    "parent flag set without its declared child flag (e.g., "
                                                    "MetaFlags::Quantized set without MetaFlags::HasScale).  In "
                                                    "every case the bits_ field carries content the wrapper's "
                                                    "consumers (test() / set() / popcount() / serialization) "
                                                    "depend on EXCLUDING — silent acceptance routes the bad value "
                                                    "into the model state.";
    static constexpr tag_text_literal remediation = "Audit the producer of the failing bit-pattern.  For "
                                                    "from_raw() deserialization paths: validate the on-disk word "
                                                    "against the current E's full mask BEFORE constructing the "
                                                    "Bits<E> (compare against the OR of all enumerators), and "
                                                    "reject loads from older schema versions through a separate "
                                                    "migration path.  For mutual-exclusion / subsumption "
                                                    "violations: trace the set() / unset() / toggle() call site "
                                                    "that established the invalid combination; the bug is usually "
                                                    "an early-return that skipped the unset() of the conflicting "
                                                    "flag.  Bits<E> has no parameter for such an invariant, so the "
                                                    "permanent fix is a validated owner of the word that routes "
                                                    "each mutation through a guarded transition, which fails loud "
                                                    "at the source.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "fixy::Bits<EnumType> wraps a flag word over a scoped enum, and "
        "it checks none of the invariants that follow.  Three runtime "
        "invariants route through this tag: (a) "
        "from_raw(b) admitting a deserialized word with bits outside "
        "E's declared mask (post-schema-tightening drift, e.g., an "
        "on-disk word survives an enum-tightening upgrade and now "
        "carries a bit no current enumerator names); (b) mutual-"
        "exclusion violation — two flags declared MX-pair simultaneously "
        "set; (c) subsumption "
        "violation — parent flag set without its declared child (e.g., "
        "MetaFlags::Quantized without MetaFlags::HasScale).  Silent "
        "acceptance routes the bad value into the model state; "
        "Bits<E>'s consumers (test() / popcount() / serialization) "
        "depend on the invariant EXCLUDING these patterns.";
    static constexpr tag_text_literal symptom_pattern = "Deserialization paths reading an on-disk flag word into "
                                                        "Bits<E>::from_raw without first masking against E's full "
                                                        "enumerator OR-fold.  Mutation paths that established a flag "
                                                        "without unset()'ing the conflicting MX-peer — usually an "
                                                        "early-return that skipped the conflicting-flag clear.";
    static constexpr tag_text_literal correct_example =
        "Bits<E>::from_raw(raw & kValidMask)  // kValidMask is the OR of every enumerator of E";
    static constexpr tag_text_literal violating_example = "Bits<E>::from_raw(raw)  // unmasked deserialize";
};

struct BorrowedBoundsViolation : tag_base {
    static constexpr tag_text_literal name = "BorrowedBoundsViolation";
    static constexpr tag_text_literal description = "fixy::Borrowed<T, Source> (include/fixy/Borrowed.h) received "
                                                    "an accessor call outside its bounds.  Its accessors carry no "
                                                    "contract clause and forward to std::span, whose behavior is "
                                                    "undefined outside the bounds.  Concrete failure modes: (a) "
                                                    "operator[](i) where i >= size(); (b) subview(offset, count) "
                                                    "where offset + count > size(); (c) front() or back() on an "
                                                    "empty Borrowed.  In "
                                                    "every case the consumer reads memory belonging to whatever "
                                                    "follows the source object — common consequences are torn "
                                                    "reads against a sibling field or a SIGBUS at the end of the "
                                                    "Source's mapped region.  The lifetime tag prevents use-after-"
                                                    "destruction of the source; this diagnostic covers the bounds "
                                                    "axis the lifetime gate is silent on.";
    static constexpr tag_text_literal remediation = "Audit the indexing site for missing size()-comparisons.  The "
                                                    "canonical Borrowed iteration idiom uses the range-based for "
                                                    "or std::ranges algorithms which derive bounds from "
                                                    "begin() / end() — operator[] and subview() are escape hatches "
                                                    "for index-arithmetic call sites that already proved the "
                                                    "in-range invariant by other means.  Permanent fix: rewrite "
                                                    "the indexing site through size()-aware iteration, or add a "
                                                    "CRUCIBLE_PRE(i < size()) ahead of the operator[] call (the "
                                                    "pre catches at consteval AND under enforce semantic at "
                                                    "runtime — see foundation/contracts/Pre.h).  For subview: "
                                                    "compare offset + count with size() before the call.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
        "fixy::Borrowed<T, Source> forwards operator[], subview(), front() "
        "and back() to std::span with no contract clause "
        "(include/fixy/Borrowed.h).  The lifetime tag prevents "
        "use-after-destruction of the source; this diagnostic covers "
        "the bounds axis the lifetime gate is silent on.  An out-of-"
        "bounds operator[] reads memory belonging to whatever follows "
        "the source — typical consequences: torn read against a sibling "
        "field, SIGBUS at the end of the Source's mapped region, or "
        "(under ASAN) a heap-buffer-overflow report rooted at the "
        "indexing site rather than the missing size() guard.";
    static constexpr tag_text_literal symptom_pattern = "Index-arithmetic call sites using operator[] / subview that "
                                                        "rely on caller-side size() comparison — refactoring breaks "
                                                        "the comparison without breaking the indexing.  A subview with "
                                                        "offset + count > size() that no caller check stops at the "
                                                        "boundary.";
    static constexpr tag_text_literal correct_example = "if (i < b.size()) use(b[i]);  // explicit guard";
    static constexpr tag_text_literal violating_example =
        "for (size_t i = 0; i <= b.size(); ++i) use(b[i]);  // off-by-one";
};

struct AllocationSizeOverflow : tag_base {
    static constexpr tag_text_literal name = "AllocationSizeOverflow";
    static constexpr tag_text_literal description = "The byte size of an allocation did not fit in std::size_t.  The "
                                                    "size is the element count times the element size, plus the "
                                                    "slack that rounds it up to the alignment.  One of these steps "
                                                    "wrapped, or the result is more than the bound of the buffer.  "
                                                    "foundation::AlignedBuffer and foundation::SwissTableBuffer "
                                                    "calculate their sizes this way, and each stops the process "
                                                    "before it asks the heap for storage.";
    static constexpr tag_text_literal remediation = "Find the caller that supplied the count, and make sure that "
                                                    "the count comes from a bounded source.  A count that comes "
                                                    "from a file, from the network or from a subtraction that can "
                                                    "wrap is the usual cause.  Validate the count against the "
                                                    "capacity that the design permits before the call, and refuse "
                                                    "a larger value with an error return.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters = "A wrapped size is smaller than the storage that the count "
                                                         "needs.  The allocation then succeeds, and each write past "
                                                         "the small block goes into memory that another object owns.  "
                                                         "The damage shows far from its cause, often in a different "
                                                         "thread.  The size path stops the process before the "
                                                         "allocation, because no caller can recover a buffer of the "
                                                         "size that it asked for.";
    static constexpr tag_text_literal symptom_pattern = "An abort at the first allocation after a count is read from "
                                                        "untrusted bytes, such as a header field of a trace file.  Or "
                                                        "an abort after a subtraction of two sizes wraps and gives a "
                                                        "count near the top of std::size_t.";
    static constexpr tag_text_literal correct_example =
        "if (count > kMaxSlots) return std::unexpected(Error::TooLarge);  // bounded first";
    static constexpr tag_text_literal violating_example =
        "auto buf = AlignedBuffer<Slot>::allocate(header.count);  // count read from the file";
};

}  // namespace foundation::diag
