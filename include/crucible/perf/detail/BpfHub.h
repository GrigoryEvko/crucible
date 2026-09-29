#pragma once

// The load sequence and the mapped-map plumbing that the BPF facades of
// the perf layer share.  A facade names its object, its maps and its
// minimum attach count in one spec, and the code here does the rest, so a
// fix to the sequence reaches every facade at once.

#include <crucible/perf/SchedSwitch.h>  // TimelineHeader, TIMELINE_CAPACITY
#include <crucible/perf/detail/BpfLoader.h>

#include <fixy/Borrowed.h>
#include <fixy/Mutation.h>
#include <fixy/OwnedMmap.h>
#include <fixy/Refined.h>
#include <fixy/os/Mmap.h>
#include <foundation/Lifetime.h>
#include <foundation/Pinned.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <unistd.h>

#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inplace_vector>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace crucible::perf::detail {

// One line on stderr about a facade that did not come up.  `what` names
// the step or the map, and `why` says what went wrong and what to do.
inline void report_unavailable(const char* facade, const char* what, const char* why, int err = 0) noexcept {
    if (quiet()) return;
    if (err != 0) {
        std::fprintf(stderr, "[crucible::perf] %s unavailable: %s %s (%s)\n", facade, what, why, std::strerror(err));
    } else {
        std::fprintf(stderr, "[crucible::perf] %s unavailable: %s %s\n", facade, what, why);
    }
}

// What a facade tells the loader about its BPF object.
struct LoadSpec {
    // The name in every report line, such as "sched_switch".
    const char* facade = nullptr;
    // The libbpf object name, such as "crucible_sched_switch".
    const char* object_name = nullptr;
    std::span<const unsigned char> bytecode{};
    // One missing tracepoint fails the load of the whole object, so the
    // loader turns off each program whose tracepoint the kernel lacks.  A
    // tp_btf object skips the probe, because its BTF lookup at load time
    // is the check.
    bool probe_tracepoints = true;
    // The advice printed when bpf_object__load fails.
    const char* load_advice = "(apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; verifier rejected, missing CAP_BPF, "
                              "or kernel too old)";
    // A facade with fewer attached programs than this is unavailable.
    std::size_t min_attached = 1;
    // The advice printed when fewer than min_attached programs attach.
    const char* attach_advice = nullptr;
};

// A BPF object opened from embedded bytecode and loaded, with the links of
// the programs that attached.  It owns the object and the links, and its
// destructor detaches the links and closes the object.  Each program
// settles once, as one kept link or as one failure, and settle() refuses
// an outcome past MaxLinks.  attached() and attach_failures() therefore
// stay within the bound that they publish.  MaxLinks is an int because
// the facades publish the bound as bounded_above of an int literal.
template <int MaxLinks>
class BpfObject : ::foundation::NonMovable<BpfObject<MaxLinks>> {
    static_assert(MaxLinks > 0, "A BPF facade needs at least one link slot.");

public:
    BpfObject() noexcept = default;

    ~BpfObject() {
        for (bpf_link* link : links_)
            bpf_link__destroy(link);
        if (obj_ != nullptr) bpf_object__close(obj_);
    }

    // Loads the object and attaches every program to the target that its
    // section names.  False means that the facade is unavailable, and the
    // reason is already reported.  What a failed load opened is released
    // by the destructor.
    [[nodiscard]] bool load(const LoadSpec& spec) noexcept { return open_and_load(spec) && attach_all_(spec); }

    // Opens the bytecode, writes this process's tgid into target_tgid,
    // turns off the programs whose tracepoints are missing, loads, and adds
    // the calling thread to our_tids.  A facade whose programs need an
    // attach target that the section cannot name, such as a perf event fd,
    // calls this and then settles each program itself.
    [[nodiscard]] bool open_and_load(const LoadSpec& spec) noexcept {
        install_libbpf_log_cb_once();

        bpf_object_open_opts opts{};
        opts.sz = sizeof(opts);
        opts.object_name = spec.object_name;
        bpf_object* const opened = bpf_object__open_mem(spec.bytecode.data(), spec.bytecode.size(), &opts);
        // libbpf returns null or a pointer that encodes an errno.  Closing
        // the second would dereference the encoded integer, so it is
        // dropped here and never stored.
        if (opened == nullptr || libbpf_get_error(opened) != 0) {
            report_unavailable(spec.facade, "bpf_object__open_mem", "failed (corrupt embedded bytecode; rebuild)",
                               libbpf_errno(opened, errno));
            return false;
        }
        obj_ = opened;

        patch_target_tgid_();
        if (spec.probe_tracepoints) disable_unavailable_programs(obj_);
        if (const int err = bpf_object__load(obj_); err != 0) {
            report_unavailable(spec.facade, "bpf_object__load", spec.load_advice, -err);
            return false;
        }
        register_calling_thread_();
        return true;
    }

    // Records the outcome of one attach.  A clean link is kept.  Null, or a
    // pointer that encodes a negative errno, counts as one failure and is
    // not destroyed, because destroying the encoded form dereferences the
    // integer.  True means that the link is kept.
    bool settle(bpf_link* link) noexcept {
        CRUCIBLE_PRE(links_.size() + attach_failures_.get() < static_cast<std::size_t>(MaxLinks));
        if (link == nullptr || libbpf_get_error(link) != 0) {
            attach_failures_.bump();
            return false;
        }
        if (links_.try_push_back(link) == nullptr) {
            bpf_link__destroy(link);
            attach_failures_.bump();
            return false;
        }
        return true;
    }

    // A facade with fewer than min_attached kept links is unavailable, and
    // the reason is reported here.
    [[nodiscard]] bool require_attached(const LoadSpec& spec) const noexcept {
        if (links_.size() >= spec.min_attached) return true;
        report_unavailable(spec.facade, "attach:", spec.attach_advice);
        return false;
    }

    [[nodiscard]] bpf_map* find_map(const char* name) const noexcept {
        return bpf_object__find_map_by_name(obj_, name);
    }

    [[nodiscard]] bpf_program* find_program(const char* name) const noexcept {
        return bpf_object__find_program_by_name(obj_, name);
    }

    [[nodiscard]] ::fixy::MaxBounded<MaxLinks, std::size_t> attached() const noexcept {
        return ::fixy::mint_refined<::fixy::bounded_above<MaxLinks>>(links_.size());
    }

    [[nodiscard]] ::fixy::MaxBounded<MaxLinks, std::size_t> attach_failures() const noexcept {
        return ::fixy::mint_refined<::fixy::bounded_above<MaxLinks>>(attach_failures_.get());
    }

    // A facade calls this once its maps are in place, so a partial attach
    // is reported only for a facade that did come up.
    void report_partial(const char* facade) const noexcept {
        if (quiet() || attach_failures_.get() == 0) return;
        std::fprintf(stderr,
                     "[crucible::perf] %s partial: %zu program(s) failed to attach "
                     "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
                     facade, attach_failures_.get());
    }

private:
    // Every object declares target_tgid as the first variable of its
    // .rodata, so the tgid goes at offset 0.  The section holds a handful
    // of bytes, and the copy runs once per load.
    void patch_target_tgid_() noexcept {
        bpf_map* const rodata = find_rodata(obj_);
        if (rodata == nullptr) return;
        std::size_t size = 0;
        const void* const current = bpf_map__initial_value(rodata, &size);
        if (current == nullptr || size < sizeof(uint32_t)) return;
        std::string rewritten(static_cast<const char*>(current), size);
        const uint32_t tgid = current_tgid().value();
        std::memcpy(rewritten.data(), &tgid, sizeof(tgid));
        (void)bpf_map__set_initial_value(rodata, rewritten.data(), size);
    }

    // A sched_switch program runs in the context of the task that switches
    // out, so it recognises one of our own threads switching in by its tid
    // in our_tids.  An object without that map needs nothing here.
    void register_calling_thread_() noexcept {
        bpf_map* const our_tids = find_map("our_tids");
        if (our_tids == nullptr) return;
        const uint32_t tid = current_tid().value();
        const uint8_t present = 1;
        (void)bpf_map_update_elem(map_fd(our_tids).value(), &tid, &present, BPF_ANY);
    }

    // The programs are counted before any attach, so an object with more
    // programs than link slots is refused whole, and settle() never sees
    // an outcome past its bound.
    [[nodiscard]] bool attach_all_(const LoadSpec& spec) noexcept {
        std::size_t programs = 0;
        bpf_program* prog = nullptr;
        bpf_object__for_each_program(prog, obj_) {
            if (bpf_program__autoload(prog)) ++programs;
        }
        if (programs > static_cast<std::size_t>(MaxLinks)) {
            report_unavailable(spec.facade, "attach:",
                               "the object has more programs than the facade has link slots (raise its capacity)");
            return false;
        }
        bpf_object__for_each_program(prog, obj_) {
            if (!bpf_program__autoload(prog)) continue;
            bpf_link* const link = bpf_program__attach(prog);
            if (settle(link) || !verbose()) continue;
            const long err = libbpf_get_error(link);
            const char* const section = bpf_program__section_name(prog);
            std::fprintf(stderr, "[crucible::perf] %s attach failed for %s (%s)\n", spec.facade,
                         section != nullptr ? section : "<anon>",
                         std::strerror(err != 0 ? static_cast<int>(-err) : errno));
        }
        return require_attached(spec);
    }

    bpf_object* obj_ = nullptr;
    std::inplace_vector<bpf_link*, static_cast<std::size_t>(MaxLinks)> links_{};
    ::fixy::Monotonic<std::size_t> attach_failures_ = ::fixy::mint_monotonic<std::size_t>(0);
};

// The region tag of one mapped BPF map.  It carries the tag of the
// facade, so the mapping of one facade does not pass for the mapping of
// another.  Its permission row is empty, because the mapping context, and
// not the permission, carries the effects of the map call.
template <class Tag>
struct bpf_map_region final {
    using permission_row = ::foundation::effects::Row<>;
};

template <class Tag>
using ReadOnlyMapping =
    ::fixy::OwnedMmap<bpf_map_region<Tag>, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Shared>;

// A facade loads under a context that may map a BPF map read-only and
// shared: one that owns IO and Block.
template <class Ctx>
concept CtxFitsMapArray =
    ::fixy::mmap::CtxFitsRegionMint<Ctx, ::fixy::mmap::prot::ReadOnly, ::fixy::mmap::share::Shared>;

// Maps the BPF_F_MMAPABLE array map `map_name` read-only and shared,
// rounded up to whole pages.  Tag keeps one facade's mapping from passing
// for another facade's.  An empty result means that the facade is
// unavailable, and the reason is already reported.
template <class Tag, int MaxLinks, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsMapArray<Ctx>
[[nodiscard]] std::optional<ReadOnlyMapping<Tag>> map_array(Ctx const& ctx, const BpfObject<MaxLinks>& object,
                                                            const char* facade, const char* map_name,
                                                            std::size_t bytes) noexcept {
    bpf_map* const map = object.find_map(map_name);
    if (map == nullptr) {
        report_unavailable(facade, map_name, "map not found in object (bytecode and header out of sync; rebuild)");
        return std::nullopt;
    }
    // sysconf returns -1 inside some hardened sandboxes.  The rounding
    // below needs a positive power of two.
    const long page_raw = ::sysconf(_SC_PAGESIZE);
    if (page_raw <= 0 || !std::has_single_bit(static_cast<unsigned long>(page_raw))) {
        report_unavailable(facade, "sysconf(_SC_PAGESIZE)", "gave no usable page size (hardened sandbox?)", errno);
        return std::nullopt;
    }
    const auto page = static_cast<std::size_t>(page_raw);
    const std::size_t length = (bytes + page - 1) & ~(page - 1);
    // The mapping is on the erased identity of its tag.  Nothing discards
    // its pages, so no advise_release_aware asks for its brand.
    const ::foundation::permissions::Permission<bpf_map_region<Tag>> owner =
        ::foundation::permissions::mint_permission_root<bpf_map_region<Tag>>();
    auto mapped = ReadOnlyMapping<Tag>::mint_region(ctx, owner, map_fd(map).value(), length, 0);
    if (!mapped) {
        report_unavailable(facade, map_name,
                           "mmap failed (apply CAP_BPF; BPF_F_MMAPABLE needs CAP_BPF or kernel 5.5 or later)",
                           mapped.error().value());
        return std::nullopt;
    }
    return std::optional<ReadOnlyMapping<Tag>>{std::move(*mapped)};
}

// What a ring facade tells the loader beyond its object: the ring map and
// the one-element counter map beside it.
struct RingSpec {
    LoadSpec load{};
    const char* timeline_map = nullptr;
    const char* counter_map = nullptr;
    // What a caller sees when the counter map is missing, such as
    // "context_switches() returns 0".
    const char* counter_fallback = nullptr;
};

// The layout of a mapped ring that a BPF program writes: a Header with a
// write_idx, then Capacity records of type Event.
template <class Header, class Event, std::uint32_t Capacity>
struct RingLayout {
    static_assert(sizeof(Header) % alignof(Event) == 0, "The first event must start aligned after the header.");

    static constexpr std::size_t bytes = sizeof(Header) + std::size_t{Capacity} * sizeof(Event);

    // The mapping starts with the header, and the events follow it, so
    // the view covers only the events.  The mapping is untyped byte
    // storage, so the checked lifetime start begins the typed array
    // inside it.  The bit_cast drops volatile, because libstdc++ has no
    // span<const volatile T> for a non-scalar T.  A consumer does its own
    // atomic load at each field access.
    template <class Owner, class Tag>
    [[nodiscard]] static ::fixy::Borrowed<const Event, Owner>
    events(const std::optional<ReadOnlyMapping<Tag>>& mapping) noexcept {
        if (!mapping) return ::fixy::Borrowed<const Event, Owner>{};
        auto* const base = std::bit_cast<volatile uint8_t*>(mapping->data());
        auto* const first = ::foundation::lifetime::start_as_array<Event>(
                                std::bit_cast<const uint8_t*>(base + sizeof(Header)), Capacity)
                                .data();
        return ::fixy::Borrowed<const Event, Owner>{first, Capacity};
    }

    // The volatile load needs no acquire fence.  The BPF program advances
    // write_idx with a barriered add, and a timestamp marks a finished
    // event, so a reader checks it before it trusts the rest of an event.
    template <class Tag>
    [[nodiscard]] static uint64_t write_index(const std::optional<ReadOnlyMapping<Tag>>& mapping) noexcept {
        if (!mapping) return 0;
        auto* const base = std::bit_cast<volatile uint8_t*>(mapping->data());
        const volatile Header* const header =
            ::foundation::lifetime::start_as_array<Header>(std::bit_cast<const uint8_t*>(base), 1).data();
        return header->write_idx;
    }
};

// The state of a facade that reads a ring of Event records with a
// TimelineHeader in front, and one counter.  Tag keeps the ring mapping
// of one facade from passing for another's.
template <class Tag, class Event>
struct RingState : ::foundation::NonMovable<RingState<Tag, Event>> {
    using tag_type = Tag;
    using layout = RingLayout<TimelineHeader, Event, TIMELINE_CAPACITY>;
    static constexpr int kMaxLinks = 8;

    BpfObject<kMaxLinks> object{};
    std::optional<ReadOnlyMapping<Tag>> timeline{};
    // The counter map is not mmap-able, so every read costs one bpf
    // syscall through this fd.  A missing map leaves it at -1.
    Fd counter = ::fixy::mint_tagged<source::BpfMap>(-1);

    template <class Owner>
    [[nodiscard]] ::fixy::Borrowed<const Event, Owner> events() const noexcept {
        return layout::template events<Owner>(timeline);
    }

    [[nodiscard]] uint64_t write_index() const noexcept { return layout::write_index(timeline); }

    [[nodiscard]] uint64_t count() const noexcept {
        if (counter.value() < 0) return 0;
        const uint32_t key = 0;
        uint64_t value = 0;
        return bpf_map_lookup_elem(counter.value(), &key, &value) == 0 ? value : 0;
    }
};

// Loads a ring facade.  An empty pointer means that the facade is
// unavailable, and the reason is already reported.
template <class State, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsMapArray<Ctx>
[[nodiscard]] std::unique_ptr<State> load_ring(Ctx const& ctx, const RingSpec& spec) noexcept {
    auto state = std::make_unique<State>();
    if (!state->object.load(spec.load)) return nullptr;
    state->timeline = map_array<typename State::tag_type>(ctx, state->object, spec.load.facade, spec.timeline_map,
                                                          State::layout::bytes);
    if (!state->timeline) return nullptr;
    if (bpf_map* const counter = state->object.find_map(spec.counter_map); counter != nullptr) {
        state->counter = map_fd(counter);
    } else if (verbose()) {
        std::fprintf(stderr, "[crucible::perf] %s %s map missing: %s\n", spec.load.facade, spec.counter_map,
                     spec.counter_fallback);
    }
    state->object.report_partial(spec.load.facade);
    return state;
}

// A facade with no state reports zero for both attach counts.
template <class State>
[[nodiscard]] ::fixy::MaxBounded<State::kMaxLinks, std::size_t> attached_programs(const State* state) noexcept {
    return state != nullptr ? state->object.attached()
                            : ::fixy::mint_refined<::fixy::bounded_above<State::kMaxLinks>>(std::size_t{0});
}

template <class State>
[[nodiscard]] ::fixy::MaxBounded<State::kMaxLinks, std::size_t> attach_failures(const State* state) noexcept {
    return state != nullptr ? state->object.attach_failures()
                            : ::fixy::mint_refined<::fixy::bounded_above<State::kMaxLinks>>(std::size_t{0});
}

}  // namespace crucible::perf::detail
