// The member bodies of crucible/vis/SvgRenderer.h.

#include <crucible/vis/SvgRenderer.h>

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <system_error>

namespace crucible::vis {

std::string Color::to_svg() const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

void SvgRenderer::begin(float width, float height, std::string_view title) {
    buf_ += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    buf_ += "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    buf_ += "width=\"" + ftoa(width) + "\" height=\"" + ftoa(height) + "\" ";
    buf_ += "viewBox=\"0 0 " + ftoa(width) + " " + ftoa(height) + "\">\n";

    buf_ += "<rect width=\"100%\" height=\"100%\" fill=\"";
    buf_ += palette::BG.to_svg();
    buf_ += "\"/>\n";

    buf_ += "<defs>\n";
    buf_ += "  <marker id=\"arrow\" viewBox=\"0 0 10 6\" refX=\"10\" refY=\"3\" "
            "markerWidth=\"8\" markerHeight=\"6\" orient=\"auto-start-reverse\">\n";
    buf_ += "    <path d=\"M0,0 L10,3 L0,6 z\" fill=\"#94A3B8\"/>\n";
    buf_ += "  </marker>\n";
    buf_ += "  <marker id=\"skip-arrow\" viewBox=\"0 0 10 6\" refX=\"10\" refY=\"3\" "
            "markerWidth=\"8\" markerHeight=\"6\" orient=\"auto-start-reverse\">\n";
    buf_ += "    <path d=\"M0,0 L10,3 L0,6 z\" fill=\"#F97316\"/>\n";
    buf_ += "  </marker>\n";
    buf_ += "  <filter id=\"shadow\" x=\"-4%\" y=\"-4%\" width=\"108%\" height=\"112%\">\n";
    buf_ += "    <feDropShadow dx=\"0.8\" dy=\"1.2\" stdDeviation=\"1.5\" "
            "flood-opacity=\"0.12\"/>\n";
    buf_ += "  </filter>\n";
    buf_ += "</defs>\n";

    if (!title.empty()) {
        buf_ += "<text x=\"" + ftoa(width / 2) + "\" y=\"24\" ";
        buf_ += "text-anchor=\"middle\" font-family=\"Helvetica,Arial,sans-serif\" ";
        buf_ += "font-size=\"16\" font-weight=\"bold\" fill=\"#1F2937\">";
        xml_escape(title);
        buf_ += "</text>\n";
    }
}

void SvgRenderer::embed_interactivity() {
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

void SvgRenderer::begin_group(std::string_view id, float tx, float ty) {
    buf_ += "<g";
    if (!id.empty()) {
        buf_ += " id=\"";
        buf_ += id;
        buf_ += "\"";
    }
    // Comparing the bits rather than the values keeps this clean under
    // -Wfloat-equal. Only a positive zero skips the transform.
    if (std::bit_cast<std::uint32_t>(tx) != 0 || std::bit_cast<std::uint32_t>(ty) != 0) {
        buf_ += " transform=\"translate(" + ftoa(tx) + "," + ftoa(ty) + ")\"";
    }
    buf_ += ">\n";
}

void SvgRenderer::begin_block_group(std::string_view info) {
    buf_ += "<g class=\"block\"";
    if (!info.empty()) {
        buf_ += " data-info=\"";
        xml_escape(info);
        buf_ += "\"";
    }
    buf_ += " style=\"cursor:pointer\">\n";
}

void SvgRenderer::rect(float x, float y, float w, float h, Color fill, Color stroke, float rx, float stroke_width,
                       bool shadow) {
    buf_ += "<rect x=\"" + ftoa(x) + "\" y=\"" + ftoa(y) + "\" ";
    buf_ += "width=\"" + ftoa(w) + "\" height=\"" + ftoa(h) + "\" ";
    buf_ += "rx=\"" + ftoa(rx) + "\" ";
    buf_ += "fill=\"" + fill.to_svg() + "\" ";
    buf_ += "stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(stroke_width) + "\"";
    if (shadow) buf_ += " filter=\"url(#shadow)\"";
    buf_ += "/>\n";
}

void SvgRenderer::text(float x, float y, std::string_view content, float font_size, Color fill, std::string_view anchor,
                       bool bold) {
    buf_ += "<text x=\"" + ftoa(x) + "\" y=\"" + ftoa(y) + "\" ";
    buf_ += "font-family=\"Helvetica,Arial,sans-serif\" ";
    buf_ += "font-size=\"" + ftoa(font_size) + "\" ";
    buf_ += "fill=\"" + fill.to_svg() + "\" ";
    buf_ += "text-anchor=\"";
    buf_ += anchor;
    buf_ += "\"";
    if (bold) buf_ += " font-weight=\"bold\"";
    buf_ += ">";
    xml_escape(content);
    buf_ += "</text>\n";
}

void SvgRenderer::text_mono(float x, float y, std::string_view content, float font_size, Color fill) {
    buf_ += "<text x=\"" + ftoa(x) + "\" y=\"" + ftoa(y) + "\" ";
    buf_ += "font-family=\"Menlo,Consolas,monospace\" ";
    buf_ += "font-size=\"" + ftoa(font_size) + "\" ";
    buf_ += "fill=\"" + fill.to_svg() + "\">";
    xml_escape(content);
    buf_ += "</text>\n";
}

void SvgRenderer::line(float x1, float y1, float x2, float y2, Color stroke, float width, bool dashed) {
    buf_ += "<line x1=\"" + ftoa(x1) + "\" y1=\"" + ftoa(y1) + "\" ";
    buf_ += "x2=\"" + ftoa(x2) + "\" y2=\"" + ftoa(y2) + "\" ";
    buf_ += "stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(width) + "\"";
    if (dashed) buf_ += " stroke-dasharray=\"4,2\"";
    buf_ += "/>\n";
}

void SvgRenderer::arrow(float x1, float y1, float x2, float y2, Color stroke, float width, bool skip) {
    buf_ += "<line x1=\"" + ftoa(x1) + "\" y1=\"" + ftoa(y1) + "\" ";
    buf_ += "x2=\"" + ftoa(x2) + "\" y2=\"" + ftoa(y2) + "\" ";
    buf_ += "stroke=\"" + stroke.to_svg() + "\" ";
    buf_ += "stroke-width=\"" + ftoa(width) + "\" ";
    buf_ += "marker-end=\"url(#";
    buf_ += skip ? "skip-arrow" : "arrow";
    buf_ += ")\"";
    if (skip) buf_ += " stroke-dasharray=\"6,3\"";
    buf_ += "/>\n";
}

void SvgRenderer::bezier_arrow(float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2,
                               Color stroke, float width, bool skip) {
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

void SvgRenderer::orthogonal_edge(float x1, float y1, float x2, float y2, Color stroke, float width, bool skip) {
    float dy = y2 - y1;
    float dx = x2 - x1;

    if (std::abs(dx) < 2.0f) {
        arrow(x1, y1, x2, y2, stroke, width, skip);
        return;
    }

    float mid_y = y1 + dy * 0.5f;

    buf_ += "<path d=\"M" + ftoa(x1) + "," + ftoa(y1);
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

void SvgRenderer::rect_dashed(float x, float y, float w, float h, Color stroke, float rx, float stroke_width,
                              std::string_view dash) {
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

void SvgRenderer::op_node(float x, float y, float w, float h, std::string_view label, Color fill, Color border,
                          float font_size) {
    rect(x, y, w, h, fill, border, 2, 0.4f);
    float tx = x + w / 2;
    float ty = y + h / 2 + font_size * 0.35f;
    text(tx, ty, label, font_size, Color::hex(0x374151), "middle", true);
}

void SvgRenderer::block_container(float x, float y, float w, float h, std::string_view title, Color fill, Color border,
                                  std::string_view subtitle) {
    rect(x, y, w, h, fill, border, 6, 1.0f, true);
    text(x + 6, y + 14, title, 11, Color::hex(0x1F2937), "start", true);
    if (!subtitle.empty()) text_mono(x + 6, y + 24, subtitle, 7, Color::hex(0x6B7280));
}

void SvgRenderer::separator(float x, float y, float w, Color stroke) { line(x, y, x + w, y, stroke, 0.5f); }

std::string SvgRenderer::ftoa(float v) {
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 1);
    if (ec != std::errc{}) {
        return "0.0";
    }
    return {buf, ptr};
}

void SvgRenderer::xml_escape(std::string_view s) {
    for (char c : s) {
        switch (c) {
            case '<':
                buf_ += "&lt;";
                break;
            case '>':
                buf_ += "&gt;";
                break;
            case '&':
                buf_ += "&amp;";
                break;
            case '"':
                buf_ += "&quot;";
                break;
            default:
                buf_ += c;
                break;
        }
    }
}

}  // namespace crucible::vis
