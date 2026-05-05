#pragma once

// LiveTraceVisualizer: thin live wrapper around the Merkle DAG SVG adapter.
//
// GAPS-092 originally named `MerkleDag&` and `AugurMetrics::Snapshot&`, but
// the current runtime exposes TraceNode / RegionNode roots and
// augur::AugurMetricsSample. This header binds to those real surfaces.

#include <crucible/augur/Metrics.h>
#include <crucible/vis/MerkleDagAdapter.h>
#include <crucible/vis/TraceVisualizer.h>

#include <string>
#include <string_view>

namespace crucible::vis {

class LiveTraceVisualizer {
 public:
  LiveTraceVisualizer() = default;

  explicit LiveTraceVisualizer(const TraceNode& root) noexcept
      : root_{&root} {}

  explicit LiveTraceVisualizer(const RegionNode& root) noexcept
      : root_{static_cast<const TraceNode*>(&root)} {}

  LiveTraceVisualizer(const TraceNode& root,
                      augur::AugurMetricsSample metrics) noexcept
      : root_{&root}, metrics_{metrics}, has_metrics_{true} {}

  LiveTraceVisualizer(const RegionNode& root,
                      augur::AugurMetricsSample metrics) noexcept
      : root_{static_cast<const TraceNode*>(&root)},
        metrics_{metrics},
        has_metrics_{true} {}

  void update_root(const TraceNode& root) noexcept { root_ = &root; }

  void update_root(const RegionNode& root) noexcept {
    root_ = static_cast<const TraceNode*>(&root);
  }

  void update_metrics(augur::AugurMetricsSample metrics) noexcept {
    metrics_ = metrics;
    has_metrics_ = true;
  }

  [[nodiscard]] bool has_root() const noexcept { return root_ != nullptr; }

  [[nodiscard]] bool has_metrics() const noexcept { return has_metrics_; }

  [[nodiscard]] MerkleDagBlockView extract() const {
    return extract_block_view(root_);
  }

  [[nodiscard]] std::string render_svg(
      std::string_view title = "Crucible Live Trace") const {
    auto view = extract();
    return render_live_block_svg(view, title_with_metrics(title));
  }

  [[nodiscard]] std::string sample_tick(
      const TraceNode& root,
      augur::AugurMetricsSample metrics,
      std::string_view title = "Crucible Live Trace") {
    update_root(root);
    update_metrics(metrics);
    return render_svg(title);
  }

  template <typename Sink>
  [[nodiscard]] bool stream_svg(Sink&& sink,
                                std::string_view title = "Crucible Live Trace") const {
    std::string svg = render_svg(title);
    if (svg.empty()) return false;
    sink(std::string_view{svg});
    return true;
  }

 private:
  const TraceNode* root_ = nullptr;
  augur::AugurMetricsSample metrics_{};
  bool has_metrics_ = false;

  [[nodiscard]] std::string title_with_metrics(std::string_view title) const {
    std::string out{title};
    if (!has_metrics_) return out;

    const auto& metrics = metrics_.peek();
    out += " | stale=" + std::to_string(metrics_.staleness().value);
    out += " | drift=" + std::to_string(metrics.ntk_alpha_drift);
    return out;
  }
};

} // namespace crucible::vis
