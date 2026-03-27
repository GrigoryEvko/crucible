#pragma once

// SvgRenderer: direct SVG string emission for trace visualization.
//
// Pure string builder — no layout logic. Takes coordinates, colors, text
// and emits valid SVG tags. The caller (TraceVisualizer) is responsible
// for computing positions.
//
// Not hot path — std::string is appropriate.

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace crucible::vis {

// ═══════════════════════════════════════════════════════════════════
// Color — RGB hex for SVG fill/stroke
// ═══════════════════════════════════════════════════════════════════

struct Color {
  uint8_t r = 0, g = 0, b = 0;

  [[nodiscard]] constexpr Color() = default;
  [[nodiscard]] constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_)
      : r(r_), g(g_), b(b_) {}

  // From hex: Color::hex(0x4A90D9)
  [[nodiscard]] static constexpr Color hex(uint32_t rgb) {
    return {static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF)};
  }

  [[nodiscard]] std::string to_svg() const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
  }
};

// ═══════════════════════════════════════════════════════════════════
// Block kind → color palette
// ═══════════════════════════════════════════════════════════════════

namespace palette {
  inline constexpr Color GEMM_FILL   = Color::hex(0xDBEAFE);
  inline constexpr Color GEMM_BORDER = Color::hex(0x1E40AF);
  inline constexpr Color CONV_FILL   = Color::hex(0xD1FAE5);
  inline constexpr Color CONV_BORDER = Color::hex(0x065F46);
  inline constexpr Color ATTN_FILL   = Color::hex(0xFEF3C7);
  inline constexpr Color ATTN_BORDER = Color::hex(0x92400E);
  inline constexpr Color NORM_FILL   = Color::hex(0xEDE9FE);
  inline constexpr Color NORM_BORDER = Color::hex(0x5B21B6);
  inline constexpr Color ACT_FILL    = Color::hex(0xFFEDD5);
  inline constexpr Color ACT_BORDER  = Color::hex(0x9A3412);
  inline constexpr Color ELEM_FILL   = Color::hex(0xF3F4F6);
  inline constexpr Color ELEM_BORDER = Color::hex(0x374151);
  inline constexpr Color MOVE_FILL   = Color::hex(0xE0F2FE);
  inline constexpr Color MOVE_BORDER = Color::hex(0x0C4A6E);
  inline constexpr Color LOSS_FILL   = Color::hex(0xFFF1F2);
  inline constexpr Color LOSS_BORDER = Color::hex(0xBE123C);
  inline constexpr Color OPTIM_FILL  = Color::hex(0xFDF2F8);
  inline constexpr Color OPTIM_BORDER= Color::hex(0x9D174D);
  inline constexpr Color REDUCE_FILL = Color::hex(0xFEE2E2);
  inline constexpr Color REDUCE_BORDER=Color::hex(0x991B1B);
  inline constexpr Color OTHER_FILL  = Color::hex(0xF9FAFB);
  inline constexpr Color OTHER_BORDER= Color::hex(0x9CA3AF);

  // Block-level colors
  inline constexpr Color BLOCK_RESBLOCK  = Color::hex(0xD1FAE5);
  inline constexpr Color BLOCK_ATTN      = Color::hex(0xFEF3C7);
  inline constexpr Color BLOCK_MLP       = Color::hex(0xDBEAFE);
  inline constexpr Color BLOCK_CONV      = Color::hex(0xE0F2FE);
  inline constexpr Color BLOCK_LOSS      = Color::hex(0xFFF1F2);
  inline constexpr Color BLOCK_OPTIM     = Color::hex(0xFDF2F8);
  inline constexpr Color BLOCK_GENERIC   = Color::hex(0xF3F4F6);

  inline constexpr Color BLOCK_BWD_RESBLOCK = Color::hex(0xA7F3D0);
  inline constexpr Color BLOCK_BWD_ATTN     = Color::hex(0xFDE68A);
  inline constexpr Color BLOCK_BWD_MLP      = Color::hex(0xBFDBFE);
  inline constexpr Color BLOCK_BWD_GENERIC  = Color::hex(0xE5E7EB);

  inline constexpr Color EDGE_DATA_FLOW = Color::hex(0x94A3B8);
  inline constexpr Color EDGE_SKIP      = Color::hex(0xF97316);
  inline constexpr Color BG             = Color::hex(0xFCFCFC);
} // namespace palette

// ═══════════════════════════════════════════════════════════════════
// SvgRenderer — string builder for SVG documents
// ═══════════════════════════════════════════════════════════════════

class SvgRenderer {
 public:
  SvgRenderer() { buf_.reserve(64 * 1024); }

  // ── Document ─────────────────────────────────────────────────────

  void begin(float width, float height, std::string_view title = {}) {
    buf_ += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    buf_ += "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    buf_ += "width=\"" + ftoa(width) + "\" height=\"" + ftoa(height) + "\" ";
    buf_ += "viewBox=\"0 0 " + ftoa(width) + " " + ftoa(height) + "\">\n";

    // Background
    buf_ += "<rect width=\"100%\" height=\"100%\" fill=\"";
    buf_ += palette::BG.to_svg();
    buf_ += "\"/>\n";

    // Defs for arrowheads and shadows
    buf_ += "<defs>\n";
    buf_ += "  <marker id=\"arrow\" viewBox=\"0 0 10 6\" refX=\"10\" refY=\"3\" "
            "markerWidth=\"8\" markerHeight=\"6\" orient=\"auto-start-reverse\">\n";
    buf_ += "    <path d=\"M0,0 L10,3 L0,6 z\" fill=\"#94A3B8\"/>\n";
    buf_ += "  </marker>\n";
    buf_ += "  <marker id=\"skip-arrow\" viewBox=\"0 0 10 6\" refX=\"10\" refY=\"3\" "
            "markerWidth=\"8\" markerHeight=\"6\" orient=\"auto-start-reverse\">\n";
    buf_ += "    <path d=\"M0,0 L10,3 L0,6 z\" fill=\"#F97316\"/>\n";
    buf_ += "  </marker>\n";
    buf_ += "  <filter id=\"shadow\" x=\"-2%\" y=\"-2%\" width=\"104%\" height=\"104%\">\n";
    buf_ += "    <feDropShadow dx=\"0.5\" dy=\"0.5\" stdDeviation=\"1\" "
            "flood-opacity=\"0.1\"/>\n";
    buf_ += "  </filter>\n";
    buf_ += "</defs>\n";

    // Title
    if (!title.empty()) {
      buf_ += "<text x=\"" + ftoa(width / 2) + "\" y=\"24\" ";
      buf_ += "text-anchor=\"middle\" font-family=\"Helvetica,Arial,sans-serif\" ";
      buf_ += "font-size=\"16\" font-weight=\"bold\" fill=\"#1F2937\">";
      xml_escape(title);
      buf_ += "</text>\n";
    }
  }

  void end() {
    buf_ += "</svg>\n";
  }

  // Embed interactive JavaScript for hover highlights and zoom/pan.
  // Call before end(). Requires blocks to have class="block" and
  // data-id attributes matching edge data-src/data-dst.
  void embed_interactivity() {
    buf_ += R"(<script type="text/ecmascript"><![CDATA[
(function() {
  var svg = document.querySelector('svg');
  if (!svg) return;

  // ── Zoom / Pan ─────────────────────────────────────────────
  var viewBox = svg.viewBox.baseVal;
  var isPanning = false, startX, startY;

  svg.addEventListener('wheel', function(e) {
    e.preventDefault();
    var scale = e.deltaY > 0 ? 1.1 : 0.9;
    var pt = svg.createSVGPoint();
    pt.x = e.clientX; pt.y = e.clientY;
    pt = pt.matrixTransform(svg.getScreenCTM().inverse());
    viewBox.x = pt.x - (pt.x - viewBox.x) * scale;
    viewBox.y = pt.y - (pt.y - viewBox.y) * scale;
    viewBox.width *= scale;
    viewBox.height *= scale;
  });

  svg.addEventListener('mousedown', function(e) {
    if (e.button !== 0) return;
    isPanning = true;
    startX = e.clientX; startY = e.clientY;
    svg.style.cursor = 'grabbing';
  });
  svg.addEventListener('mousemove', function(e) {
    if (!isPanning) return;
    var dx = (e.clientX - startX) * viewBox.width / svg.clientWidth;
    var dy = (e.clientY - startY) * viewBox.height / svg.clientHeight;
    viewBox.x -= dx; viewBox.y -= dy;
    startX = e.clientX; startY = e.clientY;
  });
  svg.addEventListener('mouseup', function() {
    isPanning = false; svg.style.cursor = 'default';
  });
  svg.addEventListener('mouseleave', function() {
    isPanning = false; svg.style.cursor = 'default';
  });

  // ── Hover highlight ────────────────────────────────────────
  var blocks = svg.querySelectorAll('.block');
  var tooltip = document.createElementNS('http://www.w3.org/2000/svg', 'g');
  tooltip.setAttribute('id', 'tooltip');
  tooltip.style.display = 'none';
  svg.appendChild(tooltip);

  var tipBg = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
  tipBg.setAttribute('rx', '4');
  tipBg.setAttribute('fill', '#1F2937');
  tipBg.setAttribute('opacity', '0.9');
  tooltip.appendChild(tipBg);

  var tipText = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  tipText.setAttribute('fill', 'white');
  tipText.setAttribute('font-size', '10');
  tipText.setAttribute('font-family', 'Menlo,Consolas,monospace');
  tooltip.appendChild(tipText);

  blocks.forEach(function(block) {
    block.addEventListener('mouseenter', function() {
      block.style.filter = 'brightness(0.92)';
      var info = block.getAttribute('data-info') || '';
      if (info) {
        tipText.textContent = info;
        var bbox = tipText.getBBox();
        tipBg.setAttribute('x', bbox.x - 6);
        tipBg.setAttribute('y', bbox.y - 3);
        tipBg.setAttribute('width', bbox.width + 12);
        tipBg.setAttribute('height', bbox.height + 6);
        var bboxB = block.getBBox();
        tooltip.setAttribute('transform',
          'translate(' + (bboxB.x + bboxB.width + 8) + ',' + (bboxB.y + bboxB.height/2) + ')');
        tooltip.style.display = '';
      }
    });
    block.addEventListener('mouseleave', function() {
      block.style.filter = '';
      tooltip.style.display = 'none';
    });
  });
})();
]]></script>
)";
  }

  // ── Groups ───────────────────────────────────────────────────────

  void begin_group(std::string_view id = {},
                   float tx = 0, float ty = 0) {
    buf_ += "<g";
    if (!id.empty()) {
      buf_ += " id=\"";
      buf_ += id;
      buf_ += "\"";
    }
    if (tx != 0 || ty != 0) {
      buf_ += " transform=\"translate(" + ftoa(tx) + "," + ftoa(ty) + ")\"";
    }
    buf_ += ">\n";
  }

  void end_group() { buf_ += "</g>\n"; }

  // Interactive block group with class="block" and data-info tooltip.
  void begin_block_group(std::string_view info = {}) {
    buf_ += "<g class=\"block\"";
    if (!info.empty()) {
      buf_ += " data-info=\"";
      xml_escape(info);
      buf_ += "\"";
    }
    buf_ += " style=\"cursor:pointer\">\n";
  }

  // ── Rectangles ───────────────────────────────────────────────────

  void rect(float x, float y, float w, float h,
            Color fill, Color stroke,
            float rx = 4, float stroke_width = 0.8f,
            bool shadow = false) {
    buf_ += "<rect x=\"" + ftoa(x) + "\" y=\"" + ftoa(y) + "\" ";
    buf_ += "width=\"" + ftoa(w) + "\" height=\"" + ftoa(h) + "\" ";
    buf_ += "rx=\"" + ftoa(rx) + "\" ";
    buf_ += "fill=\"" + fill.to_svg() + "\" ";
    buf_ += "stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(stroke_width) + "\"";
    if (shadow) buf_ += " filter=\"url(#shadow)\"";
    buf_ += "/>\n";
  }

  // ── Text ─────────────────────────────────────────────────────────

  void text(float x, float y, std::string_view content,
            float font_size = 10, Color fill = Color::hex(0x1F2937),
            std::string_view anchor = "start",
            bool bold = false) {
    buf_ += "<text x=\"" + ftoa(x) + "\" y=\"" + ftoa(y) + "\" ";
    buf_ += "font-family=\"Helvetica,Arial,sans-serif\" ";
    buf_ += "font-size=\"" + ftoa(font_size) + "\" ";
    buf_ += "fill=\"" + fill.to_svg() + "\" ";
    buf_ += "text-anchor=\"" ;
    buf_ += anchor;
    buf_ += "\"";
    if (bold) buf_ += " font-weight=\"bold\"";
    buf_ += ">";
    xml_escape(content);
    buf_ += "</text>\n";
  }

  void text_mono(float x, float y, std::string_view content,
                 float font_size = 8, Color fill = Color::hex(0x6B7280)) {
    buf_ += "<text x=\"" + ftoa(x) + "\" y=\"" + ftoa(y) + "\" ";
    buf_ += "font-family=\"Menlo,Consolas,monospace\" ";
    buf_ += "font-size=\"" + ftoa(font_size) + "\" ";
    buf_ += "fill=\"" + fill.to_svg() + "\">";
    xml_escape(content);
    buf_ += "</text>\n";
  }

  // ── Lines and Paths ──────────────────────────────────────────────

  void line(float x1, float y1, float x2, float y2,
            Color stroke = palette::EDGE_DATA_FLOW,
            float width = 0.5f, bool dashed = false) {
    buf_ += "<line x1=\"" + ftoa(x1) + "\" y1=\"" + ftoa(y1) + "\" ";
    buf_ += "x2=\"" + ftoa(x2) + "\" y2=\"" + ftoa(y2) + "\" ";
    buf_ += "stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(width) + "\"";
    if (dashed) buf_ += " stroke-dasharray=\"4,2\"";
    buf_ += "/>\n";
  }

  // Edge with arrowhead
  void arrow(float x1, float y1, float x2, float y2,
             Color stroke = palette::EDGE_DATA_FLOW,
             float width = 0.6f, bool skip = false) {
    buf_ += "<line x1=\"" + ftoa(x1) + "\" y1=\"" + ftoa(y1) + "\" ";
    buf_ += "x2=\"" + ftoa(x2) + "\" y2=\"" + ftoa(y2) + "\" ";
    buf_ += "stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(width) + "\" ";
    buf_ += "marker-end=\"url(#" ;
    buf_ += skip ? "skip-arrow" : "arrow";
    buf_ += ")\"";
    if (skip) buf_ += " stroke-dasharray=\"6,3\"";
    buf_ += "/>\n";
  }

  // Cubic bezier edge with arrowhead
  void bezier_arrow(float x1, float y1,
                    float cx1, float cy1,
                    float cx2, float cy2,
                    float x2, float y2,
                    Color stroke = palette::EDGE_DATA_FLOW,
                    float width = 0.6f, bool skip = false) {
    buf_ += "<path d=\"M" + ftoa(x1) + "," + ftoa(y1);
    buf_ += " C" + ftoa(cx1) + "," + ftoa(cy1);
    buf_ += " " + ftoa(cx2) + "," + ftoa(cy2);
    buf_ += " " + ftoa(x2) + "," + ftoa(y2) + "\" ";
    buf_ += "fill=\"none\" stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(width) + "\" ";
    buf_ += "marker-end=\"url(#";
    buf_ += skip ? "skip-arrow" : "arrow";
    buf_ += ")\"";
    if (skip) buf_ += " stroke-dasharray=\"6,3\"";
    buf_ += "/>\n";
  }

  // Orthogonal edge: down from src, horizontal, down into dst.
  // Routes through inter-row gaps, never through blocks.
  // Uses a smooth cubic bezier with orthogonal control points.
  void orthogonal_edge(float x1, float y1, float x2, float y2,
                       Color stroke = palette::EDGE_DATA_FLOW,
                       float width = 0.5f, bool skip = false) {
    float dy = y2 - y1;
    float dx = x2 - x1;

    if (std::abs(dx) < 2.0f) {
      // Nearly vertical: straight line with arrowhead
      arrow(x1, y1, x2, y2, stroke, width, skip);
      return;
    }

    // Route: down from src → horizontal at midpoint → down into dst
    // Using smooth bezier that approximates orthogonal corners
    float mid_y = y1 + dy * 0.5f;

    // SVG path: move to start, curve to mid-horizontal, curve to end
    buf_ += "<path d=\"M" + ftoa(x1) + "," + ftoa(y1);
    // First segment: vertical down to mid_y, curving toward x2
    buf_ += " C" + ftoa(x1) + "," + ftoa(mid_y);
    buf_ += " " + ftoa(x2) + "," + ftoa(mid_y);
    buf_ += " " + ftoa(x2) + "," + ftoa(y2);
    buf_ += "\" fill=\"none\" stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(width) + "\" ";
    buf_ += "marker-end=\"url(#";
    buf_ += skip ? "skip-arrow" : "arrow";
    buf_ += ")\"";
    if (skip) buf_ += " stroke-dasharray=\"6,3\"";
    buf_ += "/>\n";
  }

  // Dashed rectangle (for cluster borders)
  void rect_dashed(float x, float y, float w, float h,
                   Color stroke, float rx = 6,
                   float stroke_width = 0.8f,
                   std::string_view dash = "6,3") {
    buf_ += "<rect x=\"" + ftoa(x) + "\" y=\"" + ftoa(y) + "\" ";
    buf_ += "width=\"" + ftoa(w) + "\" height=\"" + ftoa(h) + "\" ";
    buf_ += "rx=\"" + ftoa(rx) + "\" ";
    buf_ += "fill=\"none\" ";
    buf_ += "stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(stroke_width) + "\" ";
    buf_ += "stroke-dasharray=\"";
    buf_ += dash;
    buf_ += "\"/>\n";
  }

  // ── Op node (small rectangle inside a block) ─────────────────────

  void op_node(float x, float y, float w, float h,
               std::string_view label, Color fill, Color border,
               float font_size = 7) {
    rect(x, y, w, h, fill, border, 2, 0.4f);
    // Center text
    float tx = x + w / 2;
    float ty = y + h / 2 + font_size * 0.35f;
    text(tx, ty, label, font_size, Color::hex(0x374151), "middle", true);
  }

  // ── Block container (rounded rect with title) ────────────────────

  void block_container(float x, float y, float w, float h,
                       std::string_view title, Color fill, Color border,
                       std::string_view subtitle = {}) {
    rect(x, y, w, h, fill, border, 6, 1.0f, true);
    // Title at top
    text(x + 6, y + 14, title, 11, Color::hex(0x1F2937), "start", true);
    if (!subtitle.empty())
      text_mono(x + 6, y + 24, subtitle, 7, Color::hex(0x6B7280));
  }

  // ── Horizontal separator ─────────────────────────────────────────

  void separator(float x, float y, float w,
                 Color stroke = Color::hex(0xE5E7EB)) {
    line(x, y, x + w, y, stroke, 0.5f);
  }

  // ── Access ───────────────────────────────────────────────────────

  [[nodiscard]] const std::string& str() const { return buf_; }
  [[nodiscard]] std::string&& take() { return std::move(buf_); }

 private:
  std::string buf_;

  [[nodiscard]] static std::string ftoa(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
  }

  void xml_escape(std::string_view s) {
    for (char c : s) {
      switch (c) {
        case '<':  buf_ += "&lt;"; break;
        case '>':  buf_ += "&gt;"; break;
        case '&':  buf_ += "&amp;"; break;
        case '"':  buf_ += "&quot;"; break;
        default:   buf_ += c; break;
      }
    }
  }
};

} // namespace crucible::vis
