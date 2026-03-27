// render_trace: visualize .crtrace files as SVG.
//
// Usage:
//   render_trace <trace.crtrace> [-o output.svg] [--blocks-only]
//
// Modes:
//   --blocks-only  Block-level view (default): colored rectangles with labels,
//                  phase columns, skip connections, cluster borders, legend.
//   (default)      Full view: ops inside blocks + inter-block edges.
//
// Output: SVG file (default: <trace_name>.svg in current directory).

#include <cstdio>
#include <cstring>
#include <fstream>

#include <crucible/vis/TraceVisualizer.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
        "Usage: %s <trace.crtrace> [-o output.svg] [--blocks-only]\n",
        argv[0]);
    return 1;
  }

  const char* trace_path = argv[1];
  const char* output_path = nullptr;
  bool blocks_only = false;

  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc)
      output_path = argv[++i];
    else if (std::strcmp(argv[i], "--blocks-only") == 0)
      blocks_only = true;
  }

  // Default output: same stem with .svg extension
  std::string default_out;
  if (!output_path) {
    default_out = trace_path;
    auto dot = default_out.rfind('.');
    if (dot != std::string::npos)
      default_out = default_out.substr(0, dot);
    default_out += ".svg";
    output_path = default_out.c_str();
  }

  // Load trace
  std::fprintf(stderr, "Loading %s...\n", trace_path);
  auto trace = crucible::load_trace(trace_path);
  if (!trace) return 1;
  std::fprintf(stderr, "  %u ops, %u metas\n",
               trace->num_ops, trace->num_metas);

  // Build ops and detect blocks
  std::fprintf(stderr, "Detecting blocks...\n");
  auto ops = crucible::vis::build_ops(*trace);
  auto detection = crucible::vis::detect_blocks(*trace);
  std::fprintf(stderr, "  %zu blocks (%s)\n",
               detection.blocks.size(),
               detection.architecture == crucible::vis::Architecture::UNET
                   ? "UNet" :
               detection.architecture == crucible::vis::Architecture::VIT
                   ? "ViT" : "generic");

  // Render SVG
  std::fprintf(stderr, "Rendering...\n");
  std::string title = trace_path;
  auto slash = title.rfind('/');
  if (slash != std::string::npos) title = title.substr(slash + 1);

  std::string svg;
  if (blocks_only)
    svg = crucible::vis::render_block_svg(detection, ops, title);
  else
    svg = crucible::vis::render_full_svg(detection, ops, title);

  // Write output
  std::ofstream out(output_path);
  if (!out) {
    std::fprintf(stderr, "Error: cannot write %s\n", output_path);
    return 1;
  }
  out << svg;
  std::fprintf(stderr, "  %zu bytes -> %s\n", svg.size(), output_path);
  return 0;
}
