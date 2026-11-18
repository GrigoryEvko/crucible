#pragma once

// The Walk family: a lazy chain of steps over the elements of a run.
//
// A walk is a value: a nest of stages over a View, with no allocation and no
// type erasure.  view.walk() gives the head of a chain, and an owner gives
// its walk through view().walk().  The iteration verbs reverse(),
// windows<W>(), chunks<W>() and enumerate() of a View and of an owner give a
// walk too.  Each adapter gives a new walk, and a terminal gives the result:
//
//   map(fn)            each element through fn
//   filter(keep)       the elements that keep accepts
//   limit(count)       at most the first count elements
//   skip(count)        the elements after the first count, of an exact walk
//   zip(second)        the pairs of two walks of one fixed extent
//   try_zip(second)    the pairs of two exact walks when one of them has a
//                      dynamic extent, or LengthMismatch when their counts
//                      differ
//   flat_map(fn)       the elements of the walk that fn gives for each
//                      element, one walk after the other
//
//   fold(init, step)   step(... step(step(init, x0), x1) ..., xn-1)
//   size()             the count of an exact walk, with no walk of the
//                      elements
//   count()            the count of a walk of unknown count, by a walk of
//                      each element
//   any_of(holds)      true when holds accepts an element
//   all_of(holds)      true when holds accepts each element
//   for (x : walk)     the cursor loop
//   rest()             the View of the elements after the last run of a walk
//                      of chunks<W>(), which no run holds
//
// Each adapter and each terminal takes the walk through &&, so a chain reads
// as one expression, and a named walk moves into its next step.  The walk of
// a View borrows the elements of the View: a step gives T& or T const&, and
// it consumes nothing.  A walk lives no longer than the run that it walks.
//
// THE PUSH
//   Each terminal pushes each element through the nest, as Iterator::fold of
//   Rust does: the root stage runs the counted loop of its run, and each
//   stage calls the next stage for each element.  The optimizer inlines the
//   nest, so a chain compiles to the loop of the raw form.  The probes walk_*
//   of test/shape/core_shapes.cpp compare each shape with its raw loop.
//
// THE COUNT
//   A stage of a known count is exact: the root, map, limit, skip and zip.  An
//   exact walk gives size() and no count().  A walk with a filter or a
//   flat_map has an unknown count, and it gives count() and no size(), so the
//   cost is in the name.  A limit of an exact walk keeps the counted loop.  A
//   limit of a walk of unknown count stops the walk after count elements.
//
// THE ORDER
//   fold runs left to right, always.  No adapter changes the order of a float
//   sum.  The tree order is reduce_by_tree and sum_by_tree of
//   foundation/core/Reduce.h, and a parallel fold exists only through those
//   reductions.  fold gives the bits of its step.  GCC can swap the two
//   operands of an add, and x86 keeps the payload of one operand, so the
//   payload of a NaN can differ between two compiled steps.  A step that can
//   give a NaN states its rule, such as the canonical NaN quiet_nan_of<T> of
//   foundation/core/Number.h.
//
// THE FUNCTIONS
//   map, filter, flat_map, fold, any_of and all_of call their function
//   through a const reference, and the function does not throw.  So a
//   function holds no state that changes along the walk: such a state goes
//   into the accumulator of fold.  filter, any_of and all_of read each
//   element through a const reference.
//
// THE CURSOR LOOP
//   `for (x : walk)` gives the cursor of the root stage, wrapped by each
//   stage.  A cursor of a map or a zip is the counted loop.  A cursor of a
//   filter skips the refused elements in its step, so a loop over it is a
//   loop nest, and GCC does not vectorize it: the probe
//   walk_filter_count_cursor shows a scalar loop with a branch for each
//   element, where filter(keep).count() gives the vector loop.  That loop is
//   for a body with a side effect.  A flat_map walk has no cursor: its
//   terminals push through the nest.  A cursor of a map, a filter or a zip
//   points into its walk, so the cursor of such a temporary walk is refused.
//   A cursor of a root stage holds only places of the run.
//
// THE STAGES
//   A stage is a value of detail that gives its elements in two ways:
//
//     for_each_(sink)   pushes each element to sink, in order, with no stop.
//                       fold and count use it
//     try_each_(sink)   the same, but sink gives Flow::stop to stop the walk.
//                       any_of, all_of and a limit of a stage of unknown
//                       count use it
//
//   A stage states element_reference, the type that it gives, is_exact, and
//   extent, which is dynamic_extent unless the type proves the count.  An
//   exact stage also gives size_(), at_(position), and the smaller stages
//   limited_(count) and skipped_(count).  A stage with a cursor gives
//   begin_() and end_(), and cursor_borrows_stage states whether its cursor
//   points into the stage.  The root stages of the Region family are in
//   foundation/core/Region.h.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

// The extent of a run whose count is a value and not a part of its type.
inline constexpr std::size_t dynamic_extent = ~std::size_t{0};

// The error of an operation of two runs whose counts differ: a copy between
// two Views of a dynamic extent, and try_zip of two walks.
struct LengthMismatch final {};

template <class Stage>
class Walk;

namespace detail {

// The answer of a sink of try_each_: go on to the next element, or stop the
// walk.
enum class Flow : bool {
    go = false,
    stop = true,
};

// The element that a walk of Stage gives, as a plain value type, for a
// function that reads it through a const reference.
template <class Stage>
using walk_value_t = std::remove_cvref_t<typename Stage::element_reference>;

// A function of map: it takes an element and gives a value, with no throw.
template <class Fn, class Element>
concept WalkMapping =
    std::is_nothrow_invocable_v<Fn const&, Element> && !std::is_void_v<std::invoke_result_t<Fn const&, Element>>
    && std::is_nothrow_move_constructible_v<Fn>;

// A test of filter, any_of and all_of: it reads an element through a const
// reference and gives a bool, with no throw.
template <class Fn, class Value>
concept WalkPredicate =
    std::is_nothrow_invocable_v<Fn const&, Value const&>
    && std::same_as<std::invoke_result_t<Fn const&, Value const&>, bool> && std::is_nothrow_move_constructible_v<Fn>;

// A function of flat_map: it takes an element and gives a walk, with no
// throw.
template <class Fn, class Element>
concept WalkExpansion = std::is_nothrow_invocable_v<Fn const&, Element>
                     && ::foundation::reflect::IsInstanceOf<std::invoke_result_t<Fn const&, Element>, ^^Walk>
                     && std::is_nothrow_move_constructible_v<Fn>;

// A stage that gives the cursor of `for (x : walk)`.
template <class Stage>
concept CursorStage = requires(Stage const& stage) {
    stage.begin_();
    stage.end_();
};

// The elements of Inner through fn.  An exact Inner gives an exact stage.
template <class Inner, class Fn>
class MapStage final {
    Inner inner_;
    [[no_unique_address]] Fn fn_;

    using InnerElement = typename Inner::element_reference;

public:
    using element_reference = std::invoke_result_t<Fn const&, InnerElement>;
    static constexpr bool is_exact = Inner::is_exact;
    static constexpr std::size_t extent = Inner::extent;
    static constexpr bool cursor_borrows_stage = true;

    constexpr MapStage(Inner inner, Fn fn) noexcept : inner_{static_cast<Inner&&>(inner)}, fn_{static_cast<Fn&&>(fn)} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        auto mapped = [this, &sink] [[gnu::always_inline]] (InnerElement element) noexcept {
            sink(fn_(static_cast<InnerElement&&>(element)));
        };
        inner_.for_each_(mapped);
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        auto mapped = [this, &sink] [[gnu::always_inline]] (InnerElement element) noexcept {
            return sink(fn_(static_cast<InnerElement&&>(element)));
        };
        return inner_.try_each_(mapped);
    }

    [[nodiscard]] constexpr std::size_t size_() const noexcept
        requires Inner::is_exact
    {
        return inner_.size_();
    }

    [[nodiscard]] constexpr element_reference at_(std::size_t position) const noexcept
        requires Inner::is_exact
    {
        return fn_(inner_.at_(position));
    }

    [[nodiscard]] constexpr auto limited_(std::size_t count) && noexcept
        requires Inner::is_exact
    {
        using Limited = decltype(static_cast<Inner&&>(inner_).limited_(count));
        return MapStage<Limited, Fn>{static_cast<Inner&&>(inner_).limited_(count), static_cast<Fn&&>(fn_)};
    }

    [[nodiscard]] constexpr auto skipped_(std::size_t count) && noexcept
        requires Inner::is_exact
    {
        using Skipped = decltype(static_cast<Inner&&>(inner_).skipped_(count));
        return MapStage<Skipped, Fn>{static_cast<Inner&&>(inner_).skipped_(count), static_cast<Fn&&>(fn_)};
    }

    // The cursor of `for (x : walk)`: the cursor of Inner, and fn on each read.
    class Cursor final {
        decltype(std::declval<Inner const&>().begin_()) at_;
        MapStage const* stage_ = nullptr;

    public:
        constexpr Cursor(decltype(at_) at, MapStage const* stage) noexcept : at_{at}, stage_{stage} {}

        [[nodiscard]] constexpr element_reference operator*() const noexcept { return stage_->fn_(*at_); }

        constexpr Cursor& operator++() noexcept {
            ++at_;
            return *this;
        }

        [[nodiscard]] friend constexpr bool operator==(Cursor const& left, Cursor const& right) noexcept {
            return left.at_ == right.at_;
        }
    };

    [[nodiscard]] constexpr Cursor begin_() const noexcept
        requires CursorStage<Inner>
    {
        return Cursor{inner_.begin_(), this};
    }

    [[nodiscard]] constexpr Cursor end_() const noexcept
        requires CursorStage<Inner>
    {
        return Cursor{inner_.end_(), this};
    }
};

// The elements of Inner that keep accepts.  The count is not known.
template <class Inner, class Keep>
class FilterStage final {
    Inner inner_;
    [[no_unique_address]] Keep keep_;

public:
    using element_reference = typename Inner::element_reference;
    static constexpr bool is_exact = false;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = true;

    constexpr FilterStage(Inner inner, Keep keep) noexcept
        : inner_{static_cast<Inner&&>(inner)}, keep_{static_cast<Keep&&>(keep)} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        auto kept = [this, &sink] [[gnu::always_inline]] (element_reference element) noexcept {
            if (keep_(static_cast<walk_value_t<Inner> const&>(element))) {
                sink(static_cast<element_reference&&>(element));
            }
        };
        inner_.for_each_(kept);
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        auto kept = [this, &sink] [[gnu::always_inline]] (element_reference element) noexcept {
            if (!keep_(static_cast<walk_value_t<Inner> const&>(element))) return Flow::go;
            return sink(static_cast<element_reference&&>(element));
        };
        return inner_.try_each_(kept);
    }

    // The cursor of `for (x : walk)`.  Each step skips the refused elements,
    // so a loop over it is a loop nest (THE CURSOR LOOP).
    class Cursor final {
        decltype(std::declval<Inner const&>().begin_()) at_;
        decltype(std::declval<Inner const&>().end_()) end_;
        FilterStage const* stage_ = nullptr;

        constexpr void skip_refused_() noexcept {
            while (!(at_ == end_) && !stage_->keep_(static_cast<walk_value_t<Inner> const&>(*at_))) {
                ++at_;
            }
        }

    public:
        constexpr Cursor(decltype(at_) at, decltype(end_) end, FilterStage const* stage) noexcept
            : at_{at}, end_{end}, stage_{stage} {
            skip_refused_();
        }

        [[nodiscard]] constexpr element_reference operator*() const noexcept { return *at_; }

        constexpr Cursor& operator++() noexcept {
            ++at_;
            skip_refused_();
            return *this;
        }

        [[nodiscard]] friend constexpr bool operator==(Cursor const& left, Cursor const& right) noexcept {
            return left.at_ == right.at_;
        }
    };

    [[nodiscard]] constexpr Cursor begin_() const noexcept
        requires CursorStage<Inner>
    {
        return Cursor{inner_.begin_(), inner_.end_(), this};
    }

    [[nodiscard]] constexpr Cursor end_() const noexcept
        requires CursorStage<Inner>
    {
        return Cursor{inner_.end_(), inner_.end_(), this};
    }
};

// The first count elements of a stage of unknown count: a stop after count
// elements.  A limit of an exact stage is the smaller stage limited_(count).
template <class Inner>
class LimitStage final {
    Inner inner_;
    std::size_t count_ = 0;

public:
    using element_reference = typename Inner::element_reference;
    static constexpr bool is_exact = false;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = Inner::cursor_borrows_stage;

    constexpr LimitStage(Inner inner, std::size_t count) noexcept
        : inner_{static_cast<Inner&&>(inner)}, count_{count} {}

    // The walk stops for two causes: the sink stops it, or count elements went
    // through.  Only the first cause is a stop for the caller, so a stop of
    // any_of after a limit of a filter reaches the caller.
    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        std::size_t left = count_;
        if (left == 0) return Flow::go;
        Flow from_sink = Flow::go;
        auto counted = [&left, &sink, &from_sink] [[gnu::always_inline]] (element_reference element) noexcept {
            if (sink(static_cast<element_reference&&>(element)) == Flow::stop) {
                from_sink = Flow::stop;
                return Flow::stop;
            }
            --left;
            return left == 0 ? Flow::stop : Flow::go;
        };
        static_cast<void>(inner_.try_each_(counted));
        return from_sink;
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        auto going = [&sink] [[gnu::always_inline]] (element_reference element) noexcept {
            sink(static_cast<element_reference&&>(element));
            return Flow::go;
        };
        static_cast<void>(try_each_(going));
    }

    // The cursor of `for (x : walk)`: the cursor of Inner and the count of
    // the elements that it can still give.  Each cursor at its end equals each
    // other cursor at its end.
    class Cursor final {
        decltype(std::declval<Inner const&>().begin_()) at_;
        decltype(std::declval<Inner const&>().end_()) end_;
        std::size_t left_ = 0;

        [[nodiscard]] constexpr bool is_done_() const noexcept { return left_ == 0 || at_ == end_; }

    public:
        constexpr Cursor(decltype(at_) at, decltype(end_) end, std::size_t left) noexcept
            : at_{at}, end_{end}, left_{left} {}

        [[nodiscard]] constexpr element_reference operator*() const noexcept {
            if (left_ == 0) [[unlikely]] {
                fatal("a read through the end cursor of a limit of a walk");
            }
            return *at_;
        }

        constexpr Cursor& operator++() noexcept {
            if (left_ == 0) [[unlikely]] {
                fatal("a step past the end cursor of a limit of a walk");
            }
            ++at_;
            --left_;
            return *this;
        }

        [[nodiscard]] friend constexpr bool operator==(Cursor const& left, Cursor const& right) noexcept {
            bool const is_left_done = left.is_done_();
            if (is_left_done || right.is_done_()) return is_left_done == right.is_done_();
            return left.at_ == right.at_;
        }
    };

    [[nodiscard]] constexpr Cursor begin_() const noexcept
        requires CursorStage<Inner>
    {
        return Cursor{inner_.begin_(), inner_.end_(), count_};
    }

    [[nodiscard]] constexpr Cursor end_() const noexcept
        requires CursorStage<Inner>
    {
        return Cursor{inner_.end_(), inner_.end_(), 0};
    }
};

// The part of a pair of a zip for an element of a stage: a Ref with the brand
// of the call of zip for an element that is a reference, and the element
// itself for a value.  A Record refuses a reference part.
template <class Element, class Brand>
struct ZipPart {
    using type = Element;
};

template <class Element, class Brand>
    requires std::is_lvalue_reference_v<Element>
struct ZipPart<Element, Brand> {
    using type = Ref<std::remove_reference_t<Element>, Brand>;
};

// The pairs of two exact stages of one count, as Records of the two parts.
// The zip or the try_zip that made the stage proved the two counts equal.
// The extent is the fixed extent of one of the two, when one of them has one.
template <class First, class Second, class Brand>
class ZipStage final {
    First first_;
    Second second_;

    using FirstElement = typename First::element_reference;
    using SecondElement = typename Second::element_reference;
    using FirstPart = typename ZipPart<FirstElement, Brand>::type;
    using SecondPart = typename ZipPart<SecondElement, Brand>::type;

    template <class Element>
    [[nodiscard]] static constexpr typename ZipPart<Element, Brand>::type part_(Element&& element) noexcept {
        if constexpr (std::is_lvalue_reference_v<Element>) {
            return RefDoor::lend<Brand>(element);
        } else {
            return static_cast<Element&&>(element);
        }
    }

public:
    using element_reference = Record<FirstPart, SecondPart>;
    static constexpr bool is_exact = true;
    static constexpr std::size_t extent = First::extent != dynamic_extent ? First::extent : Second::extent;
    static constexpr bool cursor_borrows_stage = true;

    constexpr ZipStage(First first, Second second) noexcept
        : first_{static_cast<First&&>(first)}, second_{static_cast<Second&&>(second)} {}

    [[nodiscard]] constexpr std::size_t size_() const noexcept {
        if constexpr (extent != dynamic_extent) {
            return extent;
        } else {
            return first_.size_();
        }
    }

    [[nodiscard]] constexpr element_reference at_(std::size_t position) const noexcept {
        return element_reference{part_<FirstElement>(first_.at_(position)),
                                 part_<SecondElement>(second_.at_(position))};
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        std::size_t const count = size_();
        for (std::size_t position = 0; position < count; ++position) {
            sink(at_(position));
        }
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        std::size_t const count = size_();
        for (std::size_t position = 0; position < count; ++position) {
            if (sink(at_(position)) == Flow::stop) return Flow::stop;
        }
        return Flow::go;
    }

    [[nodiscard]] constexpr auto limited_(std::size_t count) && noexcept {
        using FirstLimited = decltype(static_cast<First&&>(first_).limited_(count));
        using SecondLimited = decltype(static_cast<Second&&>(second_).limited_(count));
        return ZipStage<FirstLimited, SecondLimited, Brand>{static_cast<First&&>(first_).limited_(count),
                                                            static_cast<Second&&>(second_).limited_(count)};
    }

    [[nodiscard]] constexpr auto skipped_(std::size_t count) && noexcept {
        using FirstSkipped = decltype(static_cast<First&&>(first_).skipped_(count));
        using SecondSkipped = decltype(static_cast<Second&&>(second_).skipped_(count));
        return ZipStage<FirstSkipped, SecondSkipped, Brand>{static_cast<First&&>(first_).skipped_(count),
                                                            static_cast<Second&&>(second_).skipped_(count)};
    }

    // The cursor of `for (auto [x, y] : walk)`: the position of the next
    // pair.  A read or a step at the end ends the process in each build, and
    // the optimizer removes the two checks in a loop that compares the cursor
    // with the end first.
    class Cursor final {
        ZipStage const* stage_ = nullptr;
        std::size_t at_ = 0;
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    public:
        constexpr Cursor(ZipStage const* stage, std::size_t at) noexcept : stage_{stage}, at_{at} {}

        [[nodiscard]] constexpr element_reference operator*() const noexcept {
            if (at_ >= stage_->size_()) [[unlikely]] {
                fatal("a read through the end cursor of a zip walk");
            }
            return stage_->at_(at_);
        }

        constexpr Cursor& operator++() noexcept {
            if (at_ >= stage_->size_()) [[unlikely]] {
                fatal("a step past the end cursor of a zip walk");
            }
            ++at_;
            return *this;
        }

        [[nodiscard]] friend constexpr bool operator==(Cursor const& left, Cursor const& right) noexcept {
            return left.at_ == right.at_;
        }
    };

    [[nodiscard]] constexpr Cursor begin_() const noexcept { return Cursor{this, 0}; }
    [[nodiscard]] constexpr Cursor end_() const noexcept { return Cursor{this, size_()}; }
};

// The elements of the walks that fn gives, one for each element of Inner, one
// walk after the other.  The count is not known.  The stage has no cursor
// (THE CURSOR LOOP).
template <class Inner, class Fn>
class FlatMapStage final {
    Inner inner_;
    [[no_unique_address]] Fn fn_;

    using InnerElement = typename Inner::element_reference;
    using Expanded = std::invoke_result_t<Fn const&, InnerElement>;

public:
    using element_reference = typename decltype(std::declval<Expanded&>().stage_)::element_reference;
    static constexpr bool is_exact = false;
    static constexpr std::size_t extent = dynamic_extent;
    static constexpr bool cursor_borrows_stage = true;

    constexpr FlatMapStage(Inner inner, Fn fn) noexcept
        : inner_{static_cast<Inner&&>(inner)}, fn_{static_cast<Fn&&>(fn)} {}

    template <class Sink>
    [[gnu::always_inline]] constexpr void for_each_(Sink& sink) const noexcept {
        auto expanded = [this, &sink] [[gnu::always_inline]] (InnerElement element) noexcept {
            Expanded const walk = fn_(static_cast<InnerElement&&>(element));
            walk.stage_.for_each_(sink);
        };
        inner_.for_each_(expanded);
    }

    template <class Sink>
    [[gnu::always_inline]] constexpr Flow try_each_(Sink& sink) const noexcept {
        auto expanded = [this, &sink] [[gnu::always_inline]] (InnerElement element) noexcept {
            Expanded const walk = fn_(static_cast<InnerElement&&>(element));
            return walk.stage_.try_each_(sink);
        };
        return inner_.try_each_(expanded);
    }
};

}  // namespace detail

// A walk: the stages of a chain, with the adapters and the terminals.
template <class Stage>
class [[nodiscard]] Walk final {
    Stage stage_;

    template <class Other>
    friend class Walk;
    template <class Inner, class Fn>
    friend class detail::FlatMapStage;

    using Element = typename Stage::element_reference;
    using Value = detail::walk_value_t<Stage>;

    static constexpr bool is_exact_ = Stage::is_exact;
    static constexpr bool is_fixed_ = Stage::extent != dynamic_extent;

public:
    // The walk of a stage.  A root stage comes from a View or an iteration
    // verb of a View, and each other stage from an adapter.
    constexpr explicit Walk(Stage stage) noexcept : stage_{static_cast<Stage&&>(stage)} {}

    // ── The adapters ──

    // Each element through fn.
    template <class Fn>
        requires detail::WalkMapping<Fn, Element>
    [[nodiscard]] constexpr Walk<detail::MapStage<Stage, Fn>> map(Fn fn) && noexcept {
        return Walk<detail::MapStage<Stage, Fn>>{
            detail::MapStage<Stage, Fn>{static_cast<Stage&&>(stage_), static_cast<Fn&&>(fn)}};
    }

    // The elements that keep accepts.
    template <class Keep>
        requires detail::WalkPredicate<Keep, Value>
    [[nodiscard]] constexpr Walk<detail::FilterStage<Stage, Keep>> filter(Keep keep) && noexcept {
        return Walk<detail::FilterStage<Stage, Keep>>{
            detail::FilterStage<Stage, Keep>{static_cast<Stage&&>(stage_), static_cast<Keep&&>(keep)}};
    }

    // At most the first count elements.  An exact walk gets a smaller run, so
    // the loop keeps its counted form.  Each other walk stops after count
    // elements.
    [[nodiscard]] constexpr auto limit(ElementCount count) && noexcept {
        if constexpr (is_exact_) {
            using Limited = decltype(static_cast<Stage&&>(stage_).limited_(count.raw()));
            return Walk<Limited>{static_cast<Stage&&>(stage_).limited_(count.raw())};
        } else {
            return Walk<detail::LimitStage<Stage>>{
                detail::LimitStage<Stage>{static_cast<Stage&&>(stage_), count.raw()}};
        }
    }

    // The elements after the first count, or none when count is larger.
    [[nodiscard]] constexpr auto skip(ElementCount count) && noexcept
        requires is_exact_
    {
        using Skipped = decltype(static_cast<Stage&&>(stage_).skipped_(count.raw()));
        return Walk<Skipped>{static_cast<Stage&&>(stage_).skipped_(count.raw())};
    }

    void skip(ElementCount) && noexcept
        requires(!is_exact_)
    = delete("a walk of unknown count has no position to skip to: skip the elements before the filter or the flat_map");

    // The pairs of this walk and second, two exact walks of one fixed extent.
    // The type proves the counts equal, so the walk has no check.  A part of a
    // pair is a Ref with the fresh brand of the call for an element that is a
    // reference, and the pack refuses each explicit argument.
    template <class... Refused, class Other, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0 && is_exact_ && Other::is_exact && is_fixed_ && Stage::extent == Other::extent)
    [[nodiscard]] constexpr Walk<detail::ZipStage<Stage, Other, ::foundation::brand::undeducible_brand_t<Fresh>>>
    zip(Walk<Other>&& second) && noexcept {
        return Walk<detail::ZipStage<Stage, Other, Fresh>>{
            detail::ZipStage<Stage, Other, Fresh>{static_cast<Stage&&>(stage_), static_cast<Other&&>(second.stage_)}};
    }

    template <class Other>
        requires(is_exact_ && Other::is_exact && is_fixed_ && Other::extent != dynamic_extent
                 && Stage::extent != Other::extent)
    void zip(Walk<Other>&&) && noexcept = delete("two walks of different fixed extents have different counts, so the "
                                                 "longer walk has elements with no pair");

    template <class Other>
        requires(is_exact_ && Other::is_exact && (!is_fixed_ || Other::extent == dynamic_extent))
    void zip(Walk<Other>&&) && noexcept = delete("a walk of a dynamic extent can have a different count: try_zip "
                                                 "gives LengthMismatch for two counts that differ");

    template <class Other>
        requires(!is_exact_ || !Other::is_exact)
    void zip(Walk<Other>&&) && noexcept = delete("a walk with a filter or a flat_map has no position for a pair: zip "
                                                 "the exact walks first");

    // The pairs of this walk and second, two exact walks when one of them has
    // a dynamic extent, or LengthMismatch when their counts differ.  One
    // compare.  The parts of a pair are as for zip.
    template <class... Refused, class Other, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0 && is_exact_ && Other::is_exact
                 && (!is_fixed_ || Other::extent == dynamic_extent))
    [[nodiscard]] constexpr Result<
        Walk<detail::ZipStage<Stage, Other, ::foundation::brand::undeducible_brand_t<Fresh>>>, LengthMismatch>
    try_zip(Walk<Other>&& second) && noexcept {
        if (stage_.size_() != second.stage_.size_()) {
            return err(LengthMismatch{});
        }
        return Walk<detail::ZipStage<Stage, Other, Fresh>>{
            detail::ZipStage<Stage, Other, Fresh>{static_cast<Stage&&>(stage_), static_cast<Other&&>(second.stage_)}};
    }

    template <class Other>
        requires(is_exact_ && Other::is_exact && is_fixed_ && Other::extent != dynamic_extent)
    void try_zip(Walk<Other>&&) && noexcept = delete("two walks of fixed extents prove their counts at compile "
                                                     "time: zip gives the pairs with no Result");

    template <class Other>
        requires(!is_exact_ || !Other::is_exact)
    void try_zip(Walk<Other>&&) && noexcept = delete("a walk with a filter or a flat_map has no position for a pair: "
                                                     "zip the exact walks first");

    // The elements of the walk that fn gives for each element, one walk after
    // the other.  fn gives a walk of a run that lives after the call, such as
    // a View of an element.
    template <class Fn>
        requires detail::WalkExpansion<Fn, Element>
    [[nodiscard]] constexpr Walk<detail::FlatMapStage<Stage, Fn>> flat_map(Fn fn) && noexcept {
        return Walk<detail::FlatMapStage<Stage, Fn>>{
            detail::FlatMapStage<Stage, Fn>{static_cast<Stage&&>(stage_), static_cast<Fn&&>(fn)}};
    }

    // ── The terminals ──

    // step(... step(step(init, x0), x1) ..., xn-1), left to right (THE ORDER).
    // The step takes the accumulator and an element, in the order of
    // std::ranges::fold_left and of Iterator::fold of Rust.  The two can have
    // one type, such as the floats of a sum: the stated exception of the rule
    // of strong parameter types for a fold step.  Complexity: O(n) steps.
    template <class Accumulator, class Step>
        requires std::is_nothrow_invocable_r_v<Accumulator, Step const&, Accumulator&&, Element>
              && std::is_nothrow_move_constructible_v<Accumulator> && std::is_nothrow_move_assignable_v<Accumulator>
    [[nodiscard]] constexpr Accumulator fold(Accumulator init, Step step) && noexcept {
        Accumulator acc = static_cast<Accumulator&&>(init);
        auto folded = [&acc, &step] [[gnu::always_inline]] (Element element) noexcept {
            acc = step(static_cast<Accumulator&&>(acc), static_cast<Element&&>(element));
        };
        stage_.for_each_(folded);
        return acc;
    }

    // The count of an exact walk, with no walk of the elements.  O(1).
    [[nodiscard]] constexpr std::size_t size() const noexcept
        requires is_exact_
    {
        return stage_.size_();
    }

    void size() const noexcept
        requires(!is_exact_)
    = delete("a walk with a filter or a flat_map has an unknown count: count() walks each element");

    // The count of a walk of unknown count: a walk of each element.  O(n).
    [[nodiscard]] constexpr std::size_t count() && noexcept
        requires(!is_exact_)
    {
        std::size_t counted = 0;
        auto tally = [&counted] [[gnu::always_inline]] (Element) noexcept { ++counted; };
        stage_.for_each_(tally);
        return counted;
    }

    void count() && noexcept
        requires is_exact_
    = delete("an exact walk knows its count: size() gives it with no walk of the elements");

    // The View of the elements after the last run of a walk of chunks<W>() of
    // a View, which no run holds.  O(1).
    [[nodiscard]] constexpr auto rest() const noexcept
        requires requires(Stage const& stage) { stage.rest_(); }
    {
        return stage_.rest_();
    }

    // True when holds accepts an element.  The walk stops at the first such
    // element, and an empty walk gives false.  O(n) calls of holds.
    template <class Holds>
        requires detail::WalkPredicate<Holds, Value>
    [[nodiscard]] constexpr bool any_of(Holds holds) && noexcept {
        auto probe = [&holds] [[gnu::always_inline]] (Element element) noexcept {
            return holds(static_cast<Value const&>(element)) ? detail::Flow::stop : detail::Flow::go;
        };
        return stage_.try_each_(probe) == detail::Flow::stop;
    }

    // True when holds accepts each element.  The walk stops at the first
    // element that holds refuses, and an empty walk gives true.  O(n) calls of
    // holds.
    template <class Holds>
        requires detail::WalkPredicate<Holds, Value>
    [[nodiscard]] constexpr bool all_of(Holds holds) && noexcept {
        auto probe = [&holds] [[gnu::always_inline]] (Element element) noexcept {
            return holds(static_cast<Value const&>(element)) ? detail::Flow::go : detail::Flow::stop;
        };
        return stage_.try_each_(probe) == detail::Flow::go;
    }

    // The loop `for (x : walk)` (THE CURSOR LOOP).
    [[nodiscard]] constexpr auto begin() const& noexcept
        requires detail::CursorStage<Stage>
    {
        return stage_.begin_();
    }

    [[nodiscard]] constexpr auto end() const& noexcept
        requires detail::CursorStage<Stage>
    {
        return stage_.end_();
    }

    // A cursor of a root stage holds only places of the run, so a temporary
    // walk gives it too, such as view.reverse().end().
    [[nodiscard]] constexpr auto begin() const&& noexcept
        requires detail::CursorStage<Stage> && (!Stage::cursor_borrows_stage)
    {
        return stage_.begin_();
    }

    [[nodiscard]] constexpr auto end() const&& noexcept
        requires detail::CursorStage<Stage> && (!Stage::cursor_borrows_stage)
    {
        return stage_.end_();
    }

    void begin() const&& noexcept
        requires detail::CursorStage<Stage>&& Stage::cursor_borrows_stage
    = delete(
        "a cursor of a map, a filter or a zip points into its walk, and the full expression ends a temporary walk");

    void end() const&& noexcept
        requires detail::CursorStage<Stage>&& Stage::cursor_borrows_stage
    = delete(
        "a cursor of a map, a filter or a zip points into its walk, and the full expression ends a temporary walk");
};

}  // namespace foundation::core
