/*
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "paragraph.h"

#include <hb.h>
#include <minikin/Layout.h>

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

#include "flutter/fml/logging.h"
#include "font_collection.h"
#include "font_skia.h"
#include "minikin/FontLanguageListCache.h"
#include "minikin/GraphemeBreak.h"
#include "minikin/HbFontCache.h"
#include "minikin/LayoutUtils.h"
#include "minikin/LineBreaker.h"
#include "minikin/MinikinFont.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkFont.h"
#include "third_party/skia/include/core/SkFontMetrics.h"
#include "third_party/skia/include/core/SkMaskFilter.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkTextBlob.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/effects/SkDashPathEffect.h"
#include "third_party/skia/include/effects/SkDiscretePathEffect.h"
#include "unicode/ubidi.h"
#include "unicode/utf16.h"

namespace txt {
namespace {

class GlyphTypeface {
 public:
  GlyphTypeface(sk_sp<SkTypeface> typeface, minikin::FontFakery fakery)
      : typeface_(std::move(typeface)),
        fake_bold_(fakery.isFakeBold()),
        fake_italic_(fakery.isFakeItalic()) {}

  bool operator==(GlyphTypeface& other) {
    return other.typeface_.get() == typeface_.get() &&
           other.fake_bold_ == fake_bold_ && other.fake_italic_ == fake_italic_;
  }

  bool operator!=(GlyphTypeface& other) { return !(*this == other); }

  void apply(SkFont& font) {
    font.setTypeface(typeface_);
    font.setEmbolden(fake_bold_);
    font.setSkewX(fake_italic_ ? -SK_Scalar1 / 4 : 0);
  }

 private:
  sk_sp<SkTypeface> typeface_;
  bool fake_bold_;
  bool fake_italic_;
};

GlyphTypeface GetGlyphTypeface(const minikin::Layout& layout, size_t index) {
  const FontSkia* font = static_cast<const FontSkia*>(layout.getFont(index));
  return GlyphTypeface(font->GetSkTypeface(), layout.getFakery(index));
}

// Return ranges of text that have the same typeface in the layout.
std::vector<Paragraph::Range<size_t>> GetLayoutTypefaceRuns(
    const minikin::Layout& layout) {
  std::vector<Paragraph::Range<size_t>> result;
  if (layout.nGlyphs() == 0)
    return result;
  size_t run_start = 0;
  GlyphTypeface run_typeface = GetGlyphTypeface(layout, run_start);
  for (size_t i = 1; i < layout.nGlyphs(); ++i) {
    GlyphTypeface typeface = GetGlyphTypeface(layout, i);
    if (typeface != run_typeface) {
      result.emplace_back(run_start, i);
      run_start = i;
      run_typeface = typeface;
    }
  }
  result.emplace_back(run_start, layout.nGlyphs());
  return result;
}

int GetWeight(const FontWeight weight) {
  switch (weight) {
    case FontWeight::w100:
      return 1;
    case FontWeight::w200:
      return 2;
    case FontWeight::w300:
      return 3;
    case FontWeight::w400:  // Normal.
      return 4;
    case FontWeight::w500:
      return 5;
    case FontWeight::w600:
      return 6;
    case FontWeight::w700:  // Bold.
      return 7;
    case FontWeight::w800:
      return 8;
    case FontWeight::w900:
      return 9;
    default:
      return -1;
  }
}

int GetWeight(const TextStyle& style) {
  return GetWeight(style.font_weight);
}

bool GetItalic(const TextStyle& style) {
  switch (style.font_style) {
    case FontStyle::italic:
      return true;
    case FontStyle::normal:
    default:
      return false;
  }
}

minikin::FontStyle GetMinikinFontStyle(const TextStyle& style) {
  uint32_t language_list_id =
      style.locale.empty()
          ? minikin::FontLanguageListCache::kEmptyListId
          : minikin::FontStyle::registerLanguageList(style.locale);
  return minikin::FontStyle(language_list_id, 0, GetWeight(style),
                            GetItalic(style));
}

void GetFontAndMinikinPaint(const TextStyle& style,
                            minikin::FontStyle* font,
                            minikin::MinikinPaint* paint) {
  *font = GetMinikinFontStyle(style);
  paint->size = style.font_size;
  // Divide by font size so letter spacing is pixels, not proportional to font
  // size.
  paint->letterSpacing = style.letter_spacing / style.font_size;
  paint->wordSpacing = style.word_spacing;
  paint->scaleX = 1.0f;
  // Prevent spacing rounding in Minikin. This causes jitter when switching
  // between same text content with different runs composing it, however, it
  // also produces more accurate layouts.
  paint->paintFlags |= minikin::LinearTextFlag;
  paint->fontFeatureSettings = style.font_features.GetFeatureSettings();
}

void FindWords(const std::vector<uint16_t>& text,
               size_t start,
               size_t end,
               std::vector<Paragraph::Range<size_t>>* words) {
  bool in_word = false;
  size_t word_start;
  for (size_t i = start; i < end; ++i) {
    bool is_space = minikin::isWordSpace(text[i]);
    if (!in_word && !is_space) {
      word_start = i;
      in_word = true;
    } else if (in_word && is_space) {
      words->emplace_back(word_start, i);
      in_word = false;
    }
  }
  if (in_word)
    words->emplace_back(word_start, end);
}

}  // namespace

static const float kDoubleDecorationSpacing = 3.0f;

Paragraph::GlyphPosition::GlyphPosition(double x_start,
                                        double x_advance,
                                        size_t code_unit_index,
                                        size_t code_unit_width)
    : code_units(code_unit_index, code_unit_index + code_unit_width),
      x_pos(x_start, x_start + x_advance) {}

void Paragraph::GlyphPosition::Shift(double delta) {
  x_pos.Shift(delta);
}

Paragraph::GlyphLine::GlyphLine(std::vector<GlyphPosition>&& p, size_t tcu)
    : positions(std::move(p)), total_code_units(tcu) {}

Paragraph::CodeUnitRun::CodeUnitRun(std::vector<GlyphPosition>&& p,
                                    Range<size_t> cu,
                                    Range<double> x,
                                    size_t line,
                                    const SkFontMetrics& metrics,
                                    TextDirection dir,
                                    const PlaceholderRun* placeholder)
    : positions(std::move(p)),
      code_units(cu),
      x_pos(x),
      line_number(line),
      font_metrics(metrics),
      direction(dir),
      placeholder_run(placeholder) {}

void Paragraph::CodeUnitRun::Shift(double delta) {
  x_pos.Shift(delta);
  for (GlyphPosition& position : positions)
    position.Shift(delta);
}

Paragraph::Paragraph() {
  breaker_.setLocale(icu::Locale(), nullptr);
}

Paragraph::~Paragraph() = default;

void Paragraph::SetText(std::vector<uint16_t> text, StyledRuns runs) {
  needs_layout_ = true;
  if (text.size() == 0)
    return;
  text_ = std::move(text);
  runs_ = std::move(runs);
}

void Paragraph::SetInlinePlaceholders(
    std::vector<PlaceholderRun> inline_placeholders,
    std::unordered_set<size_t> obj_replacement_char_indexes) {
  needs_layout_ = true;
  inline_placeholders_ = std::move(inline_placeholders);
  obj_replacement_char_indexes_ = std::move(obj_replacement_char_indexes);
}

bool Paragraph::ComputeLineBreaks() {
  line_ranges_.clear();
  line_widths_.clear();
  max_intrinsic_width_ = 0;

  std::vector<size_t> newline_positions;
  // Discover and add all hard breaks.
  for (size_t i = 0; i < text_.size(); ++i) {
    ULineBreak ulb = static_cast<ULineBreak>(
        u_getIntPropertyValue(text_[i], UCHAR_LINE_BREAK));
    if (ulb == U_LB_LINE_FEED || ulb == U_LB_MANDATORY_BREAK)
      newline_positions.push_back(i);
  }
  // Break at the end of the paragraph.
  newline_positions.push_back(text_.size());

  // Calculate and add any breaks due to a line being too long.
  size_t run_index = 0;
  size_t inline_placeholder_index = 0;
  for (size_t newline_index = 0; newline_index < newline_positions.size();
       ++newline_index) {
    size_t block_start =
        (newline_index > 0) ? newline_positions[newline_index - 1] + 1 : 0;
    size_t block_end = newline_positions[newline_index];
    size_t block_size = block_end - block_start;

    if (block_size == 0) {
      line_ranges_.emplace_back(block_start, block_end, block_end,
                                block_end + 1, true);
      line_widths_.push_back(0);
      continue;
    }

    // Setup breaker. We wait to set the line width in order to account for the
    // widths of the inline placeholders, which are calcualted in the loop over
    // the runs.
    breaker_.setLineWidths(0.0f, 0, width_);
    breaker_.setJustified(paragraph_style_.text_align == TextAlign::justify);
    breaker_.setStrategy(paragraph_style_.break_strategy);
    breaker_.resize(block_size);
    memcpy(breaker_.buffer(), text_.data() + block_start,
           block_size * sizeof(text_[0]));
    breaker_.setText();

    // Add the runs that include this line to the LineBreaker.
    double block_total_width = 0;
    while (run_index < runs_.size()) {
      StyledRuns::Run run = runs_.GetRun(run_index);
      if (run.start >= block_end)
        break;
      if (run.end < block_start) {
        run_index++;
        continue;
      }

      minikin::FontStyle font;
      minikin::MinikinPaint paint;
      GetFontAndMinikinPaint(run.style, &font, &paint);
      std::shared_ptr<minikin::FontCollection> collection =
          GetMinikinFontCollectionForStyle(run.style);
      if (collection == nullptr) {
        FML_LOG(INFO) << "Could not find font collection for families \""
                      << (run.style.font_families.empty()
                              ? ""
                              : run.style.font_families[0])
                      << "\".";
        return false;
      }
      size_t run_start = std::max(run.start, block_start) - block_start;
      size_t run_end = std::min(run.end, block_end) - block_start;
      bool isRtl = (paragraph_style_.text_direction == TextDirection::rtl);

      // Check if the run is an object replacement character-only run. We should
      // leave space for inline placeholder and break around it if appropriate.
      if (run.end - run.start == 1 &&
          obj_replacement_char_indexes_.count(run.start) != 0 &&
          text_[run.start] == objReplacementChar &&
          inline_placeholder_index < inline_placeholders_.size()) {
        // Is a inline placeholder run.
        PlaceholderRun placeholder_run =
            inline_placeholders_[inline_placeholder_index];
        block_total_width += placeholder_run.width;

        // Inject custom width into minikin breaker. (Uses LibTxt-minikin
        // patch).
        breaker_.setCustomCharWidth(run.start, placeholder_run.width);

        // Called with nullptr as paint in order to use the custom widths passed
        // above.
        breaker_.addStyleRun(nullptr, collection, font, run_start, run_end,
                             isRtl);
        inline_placeholder_index++;
      } else {
        // Is a regular text run.
        double run_width = breaker_.addStyleRun(&paint, collection, font,
                                                run_start, run_end, isRtl);
        block_total_width += run_width;
      }

      if (run.end > block_end)
        break;
      run_index++;
    }
    max_intrinsic_width_ = std::max(max_intrinsic_width_, block_total_width);

    size_t breaks_count = breaker_.computeBreaks();
    const int* breaks = breaker_.getBreaks();
    for (size_t i = 0; i < breaks_count; ++i) {
      size_t break_start = (i > 0) ? breaks[i - 1] : 0;
      size_t line_start = break_start + block_start;
      size_t line_end = breaks[i] + block_start;
      bool hard_break = i == breaks_count - 1;
      size_t line_end_including_newline =
          (hard_break && line_end < text_.size()) ? line_end + 1 : line_end;
      size_t line_end_excluding_whitespace = line_end;
      while (
          line_end_excluding_whitespace > line_start &&
          minikin::isLineEndSpace(text_[line_end_excluding_whitespace - 1])) {
        line_end_excluding_whitespace--;
      }
      line_ranges_.emplace_back(line_start, line_end,
                                line_end_excluding_whitespace,
                                line_end_including_newline, hard_break);
      line_widths_.push_back(breaker_.getWidths()[i]);
    }

    breaker_.finish();
  }

  return true;
}

bool Paragraph::ComputeBidiRuns(std::vector<BidiRun>* result) {
  if (text_.empty())
    return true;

  auto ubidi_closer = [](UBiDi* b) { ubidi_close(b); };
  std::unique_ptr<UBiDi, decltype(ubidi_closer)> bidi(ubidi_open(),
                                                      ubidi_closer);
  if (!bidi)
    return false;

  UBiDiLevel paraLevel = (paragraph_style_.text_direction == TextDirection::rtl)
                             ? UBIDI_RTL
                             : UBIDI_LTR;
  UErrorCode status = U_ZERO_ERROR;
  ubidi_setPara(bidi.get(), reinterpret_cast<const UChar*>(text_.data()),
                text_.size(), paraLevel, nullptr, &status);
  if (!U_SUCCESS(status))
    return false;

  int32_t bidi_run_count = ubidi_countRuns(bidi.get(), &status);
  if (!U_SUCCESS(status))
    return false;

  // Build a map of styled runs indexed by start position.
  std::map<size_t, StyledRuns::Run> styled_run_map;
  for (size_t i = 0; i < runs_.size(); ++i) {
    StyledRuns::Run run = runs_.GetRun(i);
    styled_run_map.emplace(std::make_pair(run.start, run));
  }

  for (int32_t bidi_run_index = 0; bidi_run_index < bidi_run_count;
       ++bidi_run_index) {
    int32_t bidi_run_start, bidi_run_length;
    UBiDiDirection direction = ubidi_getVisualRun(
        bidi.get(), bidi_run_index, &bidi_run_start, &bidi_run_length);
    if (!U_SUCCESS(status))
      return false;

    // Exclude the leading bidi control character if present.
    UChar32 first_char;
    U16_GET(text_.data(), 0, bidi_run_start, static_cast<int>(text_.size()),
            first_char);
    if (u_hasBinaryProperty(first_char, UCHAR_BIDI_CONTROL)) {
      bidi_run_start++;
      bidi_run_length--;
    }
    if (bidi_run_length == 0)
      continue;

    // Exclude the trailing bidi control character if present.
    UChar32 last_char;
    U16_GET(text_.data(), 0, bidi_run_start + bidi_run_length - 1,
            static_cast<int>(text_.size()), last_char);
    if (u_hasBinaryProperty(last_char, UCHAR_BIDI_CONTROL)) {
      bidi_run_length--;
    }
    if (bidi_run_length == 0)
      continue;

    size_t bidi_run_end = bidi_run_start + bidi_run_length;
    TextDirection text_direction =
        direction == UBIDI_RTL ? TextDirection::rtl : TextDirection::ltr;

    // Break this bidi run into chunks based on text style.
    std::vector<BidiRun> chunks;
    size_t chunk_start = bidi_run_start;
    while (chunk_start < bidi_run_end) {
      auto styled_run_iter = styled_run_map.upper_bound(chunk_start);
      styled_run_iter--;
      const StyledRuns::Run& styled_run = styled_run_iter->second;
      size_t chunk_end = std::min(bidi_run_end, styled_run.end);
      chunks.emplace_back(chunk_start, chunk_end, text_direction,
                          styled_run.style);
      chunk_start = chunk_end;
    }

    if (text_direction == TextDirection::ltr) {
      result->insert(result->end(), chunks.begin(), chunks.end());
    } else {
      result->insert(result->end(), chunks.rbegin(), chunks.rend());
    }
  }

  return true;
}

bool Paragraph::IsStrutValid() const {
  // Font size must be positive.
  return (paragraph_style_.strut_enabled &&
          paragraph_style_.strut_font_size >= 0);
}

void Paragraph::ComputeStrut(StrutMetrics* strut, SkFont& font) {
  strut->ascent = 0;
  strut->descent = 0;
  strut->leading = 0;
  strut->half_leading = 0;
  strut->line_height = 0;
  strut->force_strut = false;

  if (!IsStrutValid())
    return;

  // force_strut makes all lines have exactly the strut metrics, and ignores all
  // actual metrics. We only force the strut if the strut is non-zero and valid.
  strut->force_strut = paragraph_style_.force_strut_height;
  minikin::FontStyle minikin_font_style(
      0, GetWeight(paragraph_style_.strut_font_weight),
      paragraph_style_.strut_font_style == FontStyle::italic);

  std::shared_ptr<minikin::FontCollection> collection =
      font_collection_->GetMinikinFontCollectionForFamilies(
          paragraph_style_.strut_font_families, "");
  if (!collection) {
    return;
  }
  minikin::FakedFont faked_font = collection->baseFontFaked(minikin_font_style);

  if (faked_font.font != nullptr) {
    SkString str;
    static_cast<FontSkia*>(faked_font.font)
        ->GetSkTypeface()
        ->getFamilyName(&str);
    font.setTypeface(static_cast<FontSkia*>(faked_font.font)->GetSkTypeface());
    font.setSize(paragraph_style_.strut_font_size);
    SkFontMetrics strut_metrics;
    font.getMetrics(&strut_metrics);

    strut->ascent = paragraph_style_.strut_height * -strut_metrics.fAscent;
    strut->descent = paragraph_style_.strut_height * strut_metrics.fDescent;
    strut->leading =
        // Use font's leading if there is no user specified strut leading.
        paragraph_style_.strut_leading < 0
            ? strut_metrics.fLeading
            : (paragraph_style_.strut_leading *
               (strut_metrics.fDescent - strut_metrics.fAscent));
    strut->half_leading = strut->leading / 2;
    strut->line_height = strut->ascent + strut->descent + strut->leading;
  }
}

void Paragraph::ComputePlaceholder(PlaceholderRun* placeholder_run,
                                   double& ascent,
                                   double& descent) {
  if (placeholder_run != nullptr) {
    // Calculate how much to shift the ascent and descent to account
    // for the baseline choice.
    //
    // TODO(garyq): implement for various baselines. Currently only
    // supports for alphabetic and ideographic
    double baseline_adjustment = 0;
    switch (placeholder_run->baseline) {
      case TextBaseline::kAlphabetic: {
        baseline_adjustment = 0;
        break;
      }
      case TextBaseline::kIdeographic: {
        baseline_adjustment = -descent / 2;
        break;
      }
    }
    // Convert the ascent and descent from the font's to the placeholder
    // rect's.
    switch (placeholder_run->alignment) {
      case PlaceholderAlignment::kBaseline: {
        ascent = baseline_adjustment + placeholder_run->baseline_offset;
        descent = -baseline_adjustment + placeholder_run->height -
                  placeholder_run->baseline_offset;
        break;
      }
      case PlaceholderAlignment::kAboveBaseline: {
        ascent = baseline_adjustment + placeholder_run->height;
        descent = -baseline_adjustment;
        break;
      }
      case PlaceholderAlignment::kBelowBaseline: {
        descent = baseline_adjustment + placeholder_run->height;
        ascent = -baseline_adjustment;
        break;
      }
      case PlaceholderAlignment::kTop: {
        descent = placeholder_run->height - ascent;
        break;
      }
      case PlaceholderAlignment::kBottom: {
        ascent = placeholder_run->height - descent;
        break;
      }
      case PlaceholderAlignment::kMiddle: {
        double mid = (ascent - descent) / 2;
        ascent = mid + placeholder_run->height / 2;
        descent = -mid + placeholder_run->height / 2;
        break;
      }
    }
    placeholder_run->baseline_offset = ascent;
  }
}

// Implementation outline:
//
// -For each line:
//   -Compute Bidi runs, convert into line_runs (keeps in-line-range runs, adds
//   special runs)
//   -For each line_run (runs in the line):
//     -Calculate ellipsis
//     -Obtain font
//     -layout.doLayout(...), genereates glyph blobs
//     -For each glyph blob:
//       -Convert glyph blobs into pixel metrics/advances
//     -Store as paint records (for painting) and code unit runs (for metrics
//     and boxes).
//   -Apply letter spacing, alignment, justification, etc
//   -Calculate line vertical layout (ascent, descent, etc)
//   -Store per-line metrics
void Paragraph::Layout(double width, bool force) {
  double rounded_width = floor(width);
  // Do not allow calling layout multiple times without changing anything.
  if (!needs_layout_ && rounded_width == width_ && !force) {
    return;
  }

  width_ = rounded_width;

  needs_layout_ = false;

  if (!ComputeLineBreaks())
    return;

  std::vector<BidiRun> bidi_runs;
  if (!ComputeBidiRuns(&bidi_runs))
    return;

  SkFont font;
  font.setEdging(SkFont::Edging::kAntiAlias);
  font.setSubpixel(true);
  font.setHinting(SkFontHinting::kSlight);

  records_.clear();
  line_heights_.clear();
  line_baselines_.clear();
  glyph_lines_.clear();
  code_unit_runs_.clear();
  inline_placeholder_code_unit_runs_.clear();
  line_max_spacings_.clear();
  line_max_descent_.clear();
  line_max_ascent_.clear();
  max_right_ = FLT_MIN;
  min_left_ = FLT_MAX;

  minikin::Layout layout;
  SkTextBlobBuilder builder;
  double y_offset = 0;
  double prev_max_descent = 0;
  double max_word_width = 0;

  // Compute strut minimums according to paragraph_style_.
  ComputeStrut(&strut_, font);

  // Paragraph bounds tracking.
  size_t line_limit = std::min(paragraph_style_.max_lines, line_ranges_.size());
  did_exceed_max_lines_ = (line_ranges_.size() > paragraph_style_.max_lines);

  size_t placeholder_run_index = 0;
  for (size_t line_number = 0; line_number < line_limit; ++line_number) {
    const LineRange& line_range = line_ranges_[line_number];

    // Break the line into words if justification should be applied.
    std::vector<Range<size_t>> words;
    double word_gap_width = 0;
    size_t word_index = 0;
    bool justify_line =
        (paragraph_style_.text_align == TextAlign::justify &&
         line_number != line_limit - 1 && !line_range.hard_break);
    FindWords(text_, line_range.start, line_range.end, &words);
    if (justify_line) {
      if (words.size() > 1) {
        word_gap_width =
            (width_ - line_widths_[line_number]) / (words.size() - 1);
      }
    }

    // Exclude trailing whitespace from justified lines so the last visible
    // character in the line will be flush with the right margin.
    size_t line_end_index =
        (paragraph_style_.effective_align() == TextAlign::right ||
         paragraph_style_.effective_align() == TextAlign::center ||
         paragraph_style_.effective_align() == TextAlign::justify)
            ? line_range.end_excluding_whitespace
            : line_range.end;

    // Find the runs comprising this line.
    std::vector<BidiRun> line_runs;
    for (const BidiRun& bidi_run : bidi_runs) {
      // A "ghost" run is a run that does not impact the layout, breaking,
      // alignment, width, etc but is still "visible" through getRectsForRange.
      // For example, trailing whitespace on centered text can be scrolled
      // through with the caret but will not wrap the line.
      //
      // Here, we add an additional run for the whitespace, but dont
      // let it impact metrics. After layout of the whitespace run, we do not
      // add its width into the x-offset adjustment, effectively nullifying its
      // impact on the layout.
      std::unique_ptr<BidiRun> ghost_run = nullptr;
      if (paragraph_style_.ellipsis.empty() &&
          line_range.end_excluding_whitespace < line_range.end &&
          bidi_run.start() <= line_range.end &&
          bidi_run.end() > line_end_index) {
        ghost_run = std::make_unique<BidiRun>(
            std::max(bidi_run.start(), line_end_index),
            std::min(bidi_run.end(), line_range.end), bidi_run.direction(),
            bidi_run.style(), true);
      }
      // Include the ghost run before normal run if RTL
      if (bidi_run.direction() == TextDirection::rtl && ghost_run != nullptr) {
        line_runs.push_back(*ghost_run);
      }
      // Emplace a normal line run.
      if (bidi_run.start() < line_end_index &&
          bidi_run.end() > line_range.start) {
        // The run is a placeholder run.
        if (bidi_run.size() == 1 &&
            text_[bidi_run.start()] == objReplacementChar &&
            obj_replacement_char_indexes_.count(bidi_run.start()) != 0 &&
            placeholder_run_index < inline_placeholders_.size()) {
          line_runs.emplace_back(std::max(bidi_run.start(), line_range.start),
                                 std::min(bidi_run.end(), line_end_index),
                                 bidi_run.direction(), bidi_run.style(),
                                 inline_placeholders_[placeholder_run_index]);
          placeholder_run_index++;
        } else {
          line_runs.emplace_back(std::max(bidi_run.start(), line_range.start),
                                 std::min(bidi_run.end(), line_end_index),
                                 bidi_run.direction(), bidi_run.style());
        }
      }
      // Include the ghost run after normal run if LTR
      if (bidi_run.direction() == TextDirection::ltr && ghost_run != nullptr) {
        line_runs.push_back(*ghost_run);
      }
    }
    bool line_runs_all_rtl =
        line_runs.size() &&
        std::accumulate(
            line_runs.begin(), line_runs.end(), true,
            [](const bool a, const BidiRun& b) { return a && b.is_rtl(); });
    if (line_runs_all_rtl) {
      std::reverse(words.begin(), words.end());
    }

    std::vector<GlyphPosition> line_glyph_positions;
    std::vector<CodeUnitRun> line_code_unit_runs;
    std::vector<CodeUnitRun> line_inline_placeholder_code_unit_runs;
    double run_x_offset = 0;
    double justify_x_offset = 0;
    std::vector<PaintRecord> paint_records;

    for (auto line_run_it = line_runs.begin(); line_run_it != line_runs.end();
         ++line_run_it) {
      const BidiRun& run = *line_run_it;
      minikin::FontStyle minikin_font;
      minikin::MinikinPaint minikin_paint;
      GetFontAndMinikinPaint(run.style(), &minikin_font, &minikin_paint);
      font.setSize(run.style().font_size);

      std::shared_ptr<minikin::FontCollection> minikin_font_collection =
          GetMinikinFontCollectionForStyle(run.style());

      // Lay out this run.
      uint16_t* text_ptr = text_.data();
      size_t text_start = run.start();
      size_t text_count = run.end() - run.start();
      size_t text_size = text_.size();

      // Apply ellipsizing if the run was not completely laid out and this
      // is the last line (or lines are unlimited).
      const std::u16string& ellipsis = paragraph_style_.ellipsis;
      std::vector<uint16_t> ellipsized_text;
      if (ellipsis.length() && !isinf(width_) && !line_range.hard_break &&
          line_run_it == line_runs.end() - 1 &&
          (line_number == line_limit - 1 ||
           paragraph_style_.unlimited_lines())) {
        float ellipsis_width = layout.measureText(
            reinterpret_cast<const uint16_t*>(ellipsis.data()), 0,
            ellipsis.length(), ellipsis.length(), run.is_rtl(), minikin_font,
            minikin_paint, minikin_font_collection, nullptr);

        std::vector<float> text_advances(text_count);
        float text_width =
            layout.measureText(text_ptr, text_start, text_count, text_.size(),
                               run.is_rtl(), minikin_font, minikin_paint,
                               minikin_font_collection, text_advances.data());

        // Truncate characters from the text until the ellipsis fits.
        size_t truncate_count = 0;
        while (truncate_count < text_count &&
               run_x_offset + text_width + ellipsis_width > width_) {
          text_width -= text_advances[text_count - truncate_count - 1];
          truncate_count++;
        }

        ellipsized_text.reserve(text_count - truncate_count +
                                ellipsis.length());
        ellipsized_text.insert(ellipsized_text.begin(),
                               text_.begin() + run.start(),
                               text_.begin() + run.end() - truncate_count);
        ellipsized_text.insert(ellipsized_text.end(), ellipsis.begin(),
                               ellipsis.end());
        text_ptr = ellipsized_text.data();
        text_start = 0;
        text_count = ellipsized_text.size();
        text_size = text_count;

        // If there is no line limit, then skip all lines after the ellipsized
        // line.
        if (paragraph_style_.unlimited_lines()) {
          line_limit = line_number + 1;
          did_exceed_max_lines_ = true;
        }
      }

      layout.doLayout(text_ptr, text_start, text_count, text_size, run.is_rtl(),
                      minikin_font, minikin_paint, minikin_font_collection);

      if (layout.nGlyphs() == 0)
        continue;

      // When laying out RTL ghost runs, shift the run_x_offset here by the
      // advance so that the ghost run is positioned to the left of the first
      // real run of text in the line. However, since we do not want it to
      // impact the layout of real text, this advance is subsequently added
      // back into the run_x_offset after the ghost run positions have been
      // calcuated and before the next real run of text is laid out, ensuring
      // later runs are laid out in the same position as if there were no ghost
      // run.
      if (run.is_ghost() && run.is_rtl())
        run_x_offset -= layout.getAdvance();

      std::vector<float> layout_advances(text_count);
      layout.getAdvances(layout_advances.data());

      // Break the layout into blobs that share the same SkPaint parameters.
      std::vector<Range<size_t>> glyph_blobs = GetLayoutTypefaceRuns(layout);

      double word_start_position = std::numeric_limits<double>::quiet_NaN();

      // Build a Skia text blob from each group of glyphs.
      for (const Range<size_t>& glyph_blob : glyph_blobs) {
        std::vector<GlyphPosition> glyph_positions;

        GetGlyphTypeface(layout, glyph_blob.start).apply(font);
        const SkTextBlobBuilder::RunBuffer& blob_buffer =
            builder.allocRunPos(font, glyph_blob.end - glyph_blob.start);

        double justify_x_offset_delta = 0;

        for (size_t glyph_index = glyph_blob.start;
             glyph_index < glyph_blob.end;) {
          size_t cluster_start_glyph_index = glyph_index;
          uint32_t cluster = layout.getGlyphCluster(cluster_start_glyph_index);
          double glyph_x_offset;

          // Add all the glyphs in this cluster to the text blob.
          do {
            size_t blob_index = glyph_index - glyph_blob.start;
            blob_buffer.glyphs[blob_index] = layout.getGlyphId(glyph_index);

            size_t pos_index = blob_index * 2;
            blob_buffer.pos[pos_index] =
                layout.getX(glyph_index) + justify_x_offset_delta;
            blob_buffer.pos[pos_index + 1] = layout.getY(glyph_index);

            if (glyph_index == cluster_start_glyph_index)
              glyph_x_offset = blob_buffer.pos[pos_index];

            glyph_index++;
          } while (glyph_index < glyph_blob.end &&
                   layout.getGlyphCluster(glyph_index) == cluster);

          Range<int32_t> glyph_code_units(cluster, 0);
          std::vector<size_t> grapheme_code_unit_counts;
          if (run.is_rtl()) {
            if (cluster_start_glyph_index > 0) {
              glyph_code_units.end =
                  layout.getGlyphCluster(cluster_start_glyph_index - 1);
            } else {
              glyph_code_units.end = text_count;
            }
            grapheme_code_unit_counts.push_back(glyph_code_units.width());
          } else {
            if (glyph_index < layout.nGlyphs()) {
              glyph_code_units.end = layout.getGlyphCluster(glyph_index);
            } else {
              glyph_code_units.end = text_count;
            }

            // The glyph may be a ligature.  Determine how many graphemes are
            // joined into this glyph and how many input code units map to
            // each grapheme.
            size_t code_unit_count = 1;
            for (int32_t offset = glyph_code_units.start + 1;
                 offset < glyph_code_units.end; ++offset) {
              if (minikin::GraphemeBreak::isGraphemeBreak(
                      layout_advances.data(), text_ptr, text_start, text_count,
                      offset)) {
                grapheme_code_unit_counts.push_back(code_unit_count);
                code_unit_count = 1;
              } else {
                code_unit_count++;
              }
            }
            grapheme_code_unit_counts.push_back(code_unit_count);
          }
          float glyph_advance = layout.getCharAdvance(glyph_code_units.start);
          float grapheme_advance =
              glyph_advance / grapheme_code_unit_counts.size();

          glyph_positions.emplace_back(run_x_offset + glyph_x_offset,
                                       grapheme_advance,
                                       run.start() + glyph_code_units.start,
                                       grapheme_code_unit_counts[0]);

          // Compute positions for the additional graphemes in the ligature.
          for (size_t i = 1; i < grapheme_code_unit_counts.size(); ++i) {
            glyph_positions.emplace_back(
                glyph_positions.back().x_pos.end, grapheme_advance,
                glyph_positions.back().code_units.start +
                    grapheme_code_unit_counts[i - 1],
                grapheme_code_unit_counts[i]);
          }

          bool at_word_start = false;
          bool at_word_end = false;
          if (word_index < words.size()) {
            at_word_start =
                words[word_index].start == run.start() + glyph_code_units.start;
            at_word_end =
                words[word_index].end == run.start() + glyph_code_units.end;
            if (line_runs_all_rtl) {
              std::swap(at_word_start, at_word_end);
            }
          }

          if (at_word_start) {
            word_start_position = run_x_offset + glyph_x_offset;
          }

          if (at_word_end) {
            if (justify_line) {
              justify_x_offset_delta += word_gap_width;
            }
            word_index++;

            if (!isnan(word_start_position)) {
              double word_width =
                  glyph_positions.back().x_pos.end - word_start_position;
              max_word_width = std::max(word_width, max_word_width);
              word_start_position = std::numeric_limits<double>::quiet_NaN();
            }
          }
        }  // for each in glyph_blob

        if (glyph_positions.empty())
          continue;

        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        Range<double> record_x_pos(
            glyph_positions.front().x_pos.start - run_x_offset,
            glyph_positions.back().x_pos.end - run_x_offset);
        if (run.is_placeholder_run()) {
          paint_records.emplace_back(
              run.style(), SkPoint::Make(run_x_offset + justify_x_offset, 0),
              builder.make(), metrics, line_number, record_x_pos.start,
              record_x_pos.start + run.placeholder_run()->width, run.is_ghost(),
              run.placeholder_run());
          run_x_offset += run.placeholder_run()->width;
        } else {
          paint_records.emplace_back(
              run.style(), SkPoint::Make(run_x_offset + justify_x_offset, 0),
              builder.make(), metrics, line_number, record_x_pos.start,
              record_x_pos.end, run.is_ghost());
        }
        justify_x_offset += justify_x_offset_delta;

        line_glyph_positions.insert(line_glyph_positions.end(),
                                    glyph_positions.begin(),
                                    glyph_positions.end());

        // Add a record of glyph positions sorted by code unit index.
        std::vector<GlyphPosition> code_unit_positions(glyph_positions);
        std::sort(code_unit_positions.begin(), code_unit_positions.end(),
                  [](const GlyphPosition& a, const GlyphPosition& b) {
                    return a.code_units.start < b.code_units.start;
                  });

        line_code_unit_runs.emplace_back(
            std::move(code_unit_positions),
            Range<size_t>(run.start(), run.end()),
            Range<double>(glyph_positions.front().x_pos.start,
                          run.is_placeholder_run()
                              ? glyph_positions.back().x_pos.start +
                                    run.placeholder_run()->width
                              : glyph_positions.back().x_pos.end),
            line_number, metrics, run.direction(), run.placeholder_run());
        if (run.is_placeholder_run()) {
          line_inline_placeholder_code_unit_runs.push_back(
              line_code_unit_runs.back());
        }

        if (!run.is_ghost()) {
          min_left_ = std::min(min_left_, glyph_positions.front().x_pos.start);
          max_right_ = std::max(max_right_, glyph_positions.back().x_pos.end);
        }
      }  // for each in glyph_blobs

      // Do not increase x offset for LTR trailing ghost runs as it should not
      // impact the layout of visible glyphs. RTL tailing ghost runs have the
      // advance subtracted, so we do add the advance here to reset the
      // run_x_offset. We do keep the record though so GetRectsForRange() can
      // find metrics for trailing spaces.
      // if (!run.is_ghost() || run.is_rtl()) {
      if ((!run.is_ghost() || run.is_rtl()) && !run.is_placeholder_run()) {
        run_x_offset += layout.getAdvance();
      }
    }  // for each in line_runs

    // Adjust the glyph positions based on the alignment of the line.
    double line_x_offset = GetLineXOffset(run_x_offset);
    if (line_x_offset) {
      for (CodeUnitRun& code_unit_run : line_code_unit_runs) {
        code_unit_run.Shift(line_x_offset);
      }
      for (CodeUnitRun& code_unit_run :
           line_inline_placeholder_code_unit_runs) {
        code_unit_run.Shift(line_x_offset);
      }
      for (GlyphPosition& position : line_glyph_positions) {
        position.Shift(line_x_offset);
      }
    }

    size_t next_line_start = (line_number < line_ranges_.size() - 1)
                                 ? line_ranges_[line_number + 1].start
                                 : text_.size();
    glyph_lines_.emplace_back(std::move(line_glyph_positions),
                              next_line_start - line_range.start);
    code_unit_runs_.insert(code_unit_runs_.end(), line_code_unit_runs.begin(),
                           line_code_unit_runs.end());
    inline_placeholder_code_unit_runs_.insert(
        inline_placeholder_code_unit_runs_.end(),
        line_inline_placeholder_code_unit_runs.begin(),
        line_inline_placeholder_code_unit_runs.end());

    // Calculate the amount to advance in the y direction. This is done by
    // computing the maximum ascent and descent with respect to the strut.
    double max_ascent = strut_.ascent + strut_.half_leading;
    double max_descent = strut_.descent + strut_.half_leading;
    double max_unscaled_ascent = 0;
    auto update_line_metrics = [&](const SkFontMetrics& metrics,
                                   const TextStyle& style,
                                   PlaceholderRun* placeholder_run) {
      if (!strut_.force_strut) {
        double ascent =
            (-metrics.fAscent + metrics.fLeading / 2) * style.height;
        double descent =
            (metrics.fDescent + metrics.fLeading / 2) * style.height;

        ComputePlaceholder(placeholder_run, ascent, descent);

        max_ascent = std::max(ascent, max_ascent);
        max_descent = std::max(descent, max_descent);
      }

      max_unscaled_ascent = std::max(placeholder_run == nullptr
                                         ? -metrics.fAscent
                                         : placeholder_run->baseline_offset,
                                     max_unscaled_ascent);
    };
    for (const PaintRecord& paint_record : paint_records) {
      update_line_metrics(paint_record.metrics(), paint_record.style(),
                          paint_record.GetPlaceholderRun());
    }

    // If no fonts were actually rendered, then compute a baseline based on the
    // font of the paragraph style.
    if (paint_records.empty()) {
      SkFontMetrics metrics;
      TextStyle style(paragraph_style_.GetTextStyle());
      font.setTypeface(GetDefaultSkiaTypeface(style));
      font.setSize(style.font_size);
      font.getMetrics(&metrics);
      update_line_metrics(metrics, style, nullptr);
    }

    // Calculate the baselines. This is only done on the first line.
    if (line_number == 0) {
      alphabetic_baseline_ = max_ascent;
      // TODO(garyq): Ideographic baseline is currently bottom of EM
      // box, which is not correct. This should be obtained from metrics.
      // Skia currently does not support various baselines.
      ideographic_baseline_ = (max_ascent + max_descent);
    }

    line_heights_.push_back((line_heights_.empty() ? 0 : line_heights_.back()) +
                            round(max_ascent + max_descent));
    line_baselines_.push_back(line_heights_.back() - max_descent);
    y_offset += round(max_ascent + prev_max_descent);
    prev_max_descent = max_descent;

    // The max line spacing and ascent have been multiplied by -1 to make math
    // in GetRectsForRange more logical/readable.
    line_max_spacings_.push_back(max_ascent);
    line_max_descent_.push_back(max_descent);
    line_max_ascent_.push_back(max_unscaled_ascent);

    for (PaintRecord& paint_record : paint_records) {
      paint_record.SetOffset(
          SkPoint::Make(paint_record.offset().x() + line_x_offset, y_offset));
      records_.emplace_back(std::move(paint_record));
    }
  }  // for each line_number

  if (paragraph_style_.max_lines == 1 ||
      (paragraph_style_.unlimited_lines() && paragraph_style_.ellipsized())) {
    min_intrinsic_width_ = max_intrinsic_width_;
  } else {
    min_intrinsic_width_ = std::min(max_word_width, max_intrinsic_width_);
  }

  std::sort(code_unit_runs_.begin(), code_unit_runs_.end(),
            [](const CodeUnitRun& a, const CodeUnitRun& b) {
              return a.code_units.start < b.code_units.start;
            });

  longest_line_ = max_right_ - min_left_;
}

double Paragraph::GetLineXOffset(double line_total_advance) {
  if (isinf(width_))
    return 0;

  TextAlign align = paragraph_style_.effective_align();

  if (align == TextAlign::right) {
    return width_ - line_total_advance;
  } else if (align == TextAlign::center) {
    return (width_ - line_total_advance) / 2;
  } else {
    return 0;
  }
}

const ParagraphStyle& Paragraph::GetParagraphStyle() const {
  return paragraph_style_;
}

double Paragraph::GetAlphabeticBaseline() const {
  // Currently -fAscent
  return alphabetic_baseline_;
}

double Paragraph::GetIdeographicBaseline() const {
  // TODO(garyq): Currently -fAscent + fUnderlinePosition. Verify this.
  return ideographic_baseline_;
}

double Paragraph::GetMaxIntrinsicWidth() const {
  return max_intrinsic_width_;
}

double Paragraph::GetMinIntrinsicWidth() const {
  return min_intrinsic_width_;
}

size_t Paragraph::TextSize() const {
  return text_.size();
}

double Paragraph::GetHeight() const {
  return line_heights_.size() ? line_heights_.back() : 0;
}

double Paragraph::GetMaxWidth() const {
  return width_;
}

double Paragraph::GetLongestLine() const {
  return longest_line_;
}

void Paragraph::SetParagraphStyle(const ParagraphStyle& style) {
  needs_layout_ = true;
  paragraph_style_ = style;
}

void Paragraph::SetFontCollection(
    std::shared_ptr<FontCollection> font_collection) {
  font_collection_ = std::move(font_collection);
}

std::shared_ptr<minikin::FontCollection>
Paragraph::GetMinikinFontCollectionForStyle(const TextStyle& style) {
  std::string locale;
  if (!style.locale.empty()) {
    uint32_t language_list_id =
        minikin::FontStyle::registerLanguageList(style.locale);
    const minikin::FontLanguages& langs =
        minikin::FontLanguageListCache::getById(language_list_id);
    if (langs.size()) {
      locale = langs[0].getString();
    }
  }

  return font_collection_->GetMinikinFontCollectionForFamilies(
      style.font_families, locale);
}

sk_sp<SkTypeface> Paragraph::GetDefaultSkiaTypeface(const TextStyle& style) {
  std::shared_ptr<minikin::FontCollection> collection =
      GetMinikinFontCollectionForStyle(style);
  if (!collection) {
    return nullptr;
  }
  minikin::FakedFont faked_font =
      collection->baseFontFaked(GetMinikinFontStyle(style));
  return static_cast<FontSkia*>(faked_font.font)->GetSkTypeface();
}

// The x,y coordinates will be the very top left corner of the rendered
// paragraph.
void Paragraph::Paint(SkCanvas* canvas, double x, double y) {
  SkPoint base_offset = SkPoint::Make(x, y);
  SkPaint paint;
  // Paint the background first before painting any text to prevent
  // potential overlap.
  for (const PaintRecord& record : records_) {
    PaintBackground(canvas, record, base_offset);
  }
  for (const PaintRecord& record : records_) {
    if (record.style().has_foreground) {
      paint = record.style().foreground;
    } else {
      paint.reset();
      paint.setColor(record.style().color);
    }
    SkPoint offset = base_offset + record.offset();
    if (record.GetPlaceholderRun() == nullptr) {
      PaintShadow(canvas, record, offset);
      canvas->drawTextBlob(record.text(), offset.x(), offset.y(), paint);
    }
    PaintDecorations(canvas, record, base_offset);
  }
}

void Paragraph::PaintDecorations(SkCanvas* canvas,
                                 const PaintRecord& record,
                                 SkPoint base_offset) {
  if (record.style().decoration == TextDecoration::kNone)
    return;

  if (record.isGhost())
    return;

  const SkFontMetrics& metrics = record.metrics();
  SkPaint paint;
  paint.setStyle(SkPaint::kStroke_Style);
  if (record.style().decoration_color == SK_ColorTRANSPARENT) {
    paint.setColor(record.style().color);
  } else {
    paint.setColor(record.style().decoration_color);
  }
  paint.setAntiAlias(true);

  // This is set to 2 for the double line style
  int decoration_count = 1;

  // Filled when drawing wavy decorations.
  SkPath path;

  double width = record.GetRunWidth();

  SkScalar underline_thickness;
  if ((metrics.fFlags &
       SkFontMetrics::FontMetricsFlags::kUnderlineThicknessIsValid_Flag) &&
      metrics.fUnderlineThickness > 0) {
    underline_thickness = metrics.fUnderlineThickness;
  } else {
    // Backup value if the fUnderlineThickness metric is not available:
    // Divide by 14pt as it is the default size.
    underline_thickness = record.style().font_size / 14.0f;
  }
  paint.setStrokeWidth(underline_thickness *
                       record.style().decoration_thickness_multiplier);

  SkPoint record_offset = base_offset + record.offset();
  SkScalar x = record_offset.x() + record.x_start();
  SkScalar y = record_offset.y();

  // Setup the decorations.
  switch (record.style().decoration_style) {
    case TextDecorationStyle::kSolid: {
      break;
    }
    case TextDecorationStyle::kDouble: {
      decoration_count = 2;
      break;
    }
    // Note: the intervals are scaled by the thickness of the line, so it is
    // possible to change spacing by changing the decoration_thickness
    // property of TextStyle.
    case TextDecorationStyle::kDotted: {
      // Divide by 14pt as it is the default size.
      const float scale = record.style().font_size / 14.0f;
      const SkScalar intervals[] = {1.0f * scale, 1.5f * scale, 1.0f * scale,
                                    1.5f * scale};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
    // Note: the intervals are scaled by the thickness of the line, so it is
    // possible to change spacing by changing the decoration_thickness
    // property of TextStyle.
    case TextDecorationStyle::kDashed: {
      // Divide by 14pt as it is the default size.
      const float scale = record.style().font_size / 14.0f;
      const SkScalar intervals[] = {4.0f * scale, 2.0f * scale, 4.0f * scale,
                                    2.0f * scale};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
    case TextDecorationStyle::kWavy: {
      int wave_count = 0;
      double x_start = 0;
      double wavelength =
          underline_thickness * record.style().decoration_thickness_multiplier;
      path.moveTo(x, y);
      while (x_start + wavelength * 2 < width) {
        path.rQuadTo(wavelength, wave_count % 2 != 0 ? wavelength : -wavelength,
                     wavelength * 2, 0);
        x_start += wavelength * 2;
        ++wave_count;
      }
      break;
    }
  }

  // Draw the decorations.
  // Use a for loop for "kDouble" decoration style
  for (int i = 0; i < decoration_count; i++) {
    double y_offset = i * underline_thickness * kDoubleDecorationSpacing;
    double y_offset_original = y_offset;
    // Underline
    if (record.style().decoration & TextDecoration::kUnderline) {
      y_offset +=
          (metrics.fFlags &
           SkFontMetrics::FontMetricsFlags::kUnderlinePositionIsValid_Flag)
              ? metrics.fUnderlinePosition
              : underline_thickness;
      if (record.style().decoration_style != TextDecorationStyle::kWavy) {
        canvas->drawLine(x, y + y_offset, x + width, y + y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
    // Overline
    if (record.style().decoration & TextDecoration::kOverline) {
      // We subtract fAscent here because for double overlines, we want the
      // second line to be above, not below the first.
      y_offset -= metrics.fAscent;
      if (record.style().decoration_style != TextDecorationStyle::kWavy) {
        canvas->drawLine(x, y - y_offset, x + width, y - y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, -y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
    // Strikethrough
    if (record.style().decoration & TextDecoration::kLineThrough) {
      if (metrics.fFlags &
          SkFontMetrics::FontMetricsFlags::kStrikeoutThicknessIsValid_Flag)
        paint.setStrokeWidth(metrics.fStrikeoutThickness *
                             record.style().decoration_thickness_multiplier);
      // Make sure the double line is "centered" vertically.
      y_offset += (decoration_count - 1.0) * underline_thickness *
                  kDoubleDecorationSpacing / -2.0;
      y_offset +=
          (metrics.fFlags &
           SkFontMetrics::FontMetricsFlags::kStrikeoutThicknessIsValid_Flag)
              ? metrics.fStrikeoutPosition
              // Backup value if the strikeoutposition metric is not
              // available:
              : metrics.fXHeight / -2.0;
      if (record.style().decoration_style != TextDecorationStyle::kWavy) {
        canvas->drawLine(x, y + y_offset, x + width, y + y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
  }
}

void Paragraph::PaintBackground(SkCanvas* canvas,
                                const PaintRecord& record,
                                SkPoint base_offset) {
  if (!record.style().has_background)
    return;

  const SkFontMetrics& metrics = record.metrics();
  SkRect rect(SkRect::MakeLTRB(record.x_start(), metrics.fAscent,
                               record.x_end(), metrics.fDescent));
  rect.offset(base_offset + record.offset());
  canvas->drawRect(rect, record.style().background);
}

void Paragraph::PaintShadow(SkCanvas* canvas,
                            const PaintRecord& record,
                            SkPoint offset) {
  if (record.style().text_shadows.size() == 0)
    return;
  for (TextShadow text_shadow : record.style().text_shadows) {
    if (!text_shadow.hasShadow()) {
      continue;
    }

    SkPaint paint;
    paint.setColor(text_shadow.color);
    if (text_shadow.blur_radius != 0.0) {
      paint.setMaskFilter(SkMaskFilter::MakeBlur(
          kNormal_SkBlurStyle, text_shadow.blur_radius, false));
    }
    canvas->drawTextBlob(record.text(), offset.x() + text_shadow.offset.x(),
                         offset.y() + text_shadow.offset.y(), paint);
  }
}

std::vector<Paragraph::TextBox> Paragraph::GetRectsForRange(
    size_t start,
    size_t end,
    RectHeightStyle rect_height_style,
    RectWidthStyle rect_width_style) const {
  // Struct that holds calculated metrics for each line.
  struct LineBoxMetrics {
    std::vector<Paragraph::TextBox> boxes;
    // Per-line metrics for max and min coordinates for left and right boxes.
    // These metrics cannot be calculated in layout generically because of
    // selections that do not cover the whole line.
    SkScalar max_right = FLT_MIN;
    SkScalar min_left = FLT_MAX;
  };

  std::map<size_t, LineBoxMetrics> line_metrics;
  // Text direction of the first line so we can extend the correct side for
  // RectWidthStyle::kMax.
  TextDirection first_line_dir = TextDirection::ltr;

  // Lines that are actually in the requested range.
  size_t max_line = 0;
  size_t min_line = INT_MAX;
  size_t glyph_length = 0;

  // Generate initial boxes and calculate metrics.
  for (const CodeUnitRun& run : code_unit_runs_) {
    // Check to see if we are finished.
    if (run.code_units.start >= end)
      break;
    if (run.code_units.end <= start)
      continue;

    double baseline = line_baselines_[run.line_number];
    SkScalar top = baseline + run.font_metrics.fAscent;
    SkScalar bottom = baseline + run.font_metrics.fDescent;

    if (run.placeholder_run !=
        nullptr) {  // Use inline placeholder size as height.
      top = baseline - run.placeholder_run->baseline_offset;
      bottom = baseline + run.placeholder_run->height -
               run.placeholder_run->baseline_offset;
    }

    max_line = std::max(run.line_number, max_line);
    min_line = std::min(run.line_number, min_line);

    // Calculate left and right.
    SkScalar left, right;
    if (run.code_units.start >= start && run.code_units.end <= end) {
      left = run.x_pos.start;
      right = run.x_pos.end;
    } else {
      left = SK_ScalarMax;
      right = SK_ScalarMin;
      for (const GlyphPosition& gp : run.positions) {
        if (gp.code_units.start >= start && gp.code_units.end <= end) {
          left = std::min(left, static_cast<SkScalar>(gp.x_pos.start));
          right = std::max(right, static_cast<SkScalar>(gp.x_pos.end));
        } else if (gp.code_units.end == end) {
          // Calculate left and right when we are at
          // the last position of a combining character.
          glyph_length = (gp.code_units.end - gp.code_units.start) - 1;
          if (gp.code_units.start ==
              std::max<size_t>(0, (start - glyph_length))) {
            left = std::min(left, static_cast<SkScalar>(gp.x_pos.start));
            right = std::max(right, static_cast<SkScalar>(gp.x_pos.end));
          }
        }
      }
      if (left == SK_ScalarMax || right == SK_ScalarMin)
        continue;
    }
    // Keep track of the min and max horizontal coordinates over all lines. Not
    // needed for kTight.
    if (rect_width_style == RectWidthStyle::kMax) {
      line_metrics[run.line_number].max_right =
          std::max(line_metrics[run.line_number].max_right, right);
      line_metrics[run.line_number].min_left =
          std::min(line_metrics[run.line_number].min_left, left);
      if (min_line == run.line_number) {
        first_line_dir = run.direction;
      }
    }
    line_metrics[run.line_number].boxes.emplace_back(
        SkRect::MakeLTRB(left, top, right, bottom), run.direction);
  }

  // Add empty rectangles representing any newline characters within the
  // range.
  for (size_t line_number = 0; line_number < line_ranges_.size();
       ++line_number) {
    const LineRange& line = line_ranges_[line_number];
    if (line.start >= end)
      break;
    if (line.end_including_newline <= start)
      continue;
    if (line_metrics.find(line_number) == line_metrics.end()) {
      if (line.end != line.end_including_newline && line.end >= start &&
          line.end_including_newline <= end) {
        SkScalar x = line_widths_[line_number];
        // Move empty box to center if center aligned and is an empty line.
        if (x == 0 && !isinf(width_) &&
            paragraph_style_.effective_align() == TextAlign::center) {
          x = width_ / 2;
        }
        SkScalar top = (line_number > 0) ? line_heights_[line_number - 1] : 0;
        SkScalar bottom = line_heights_[line_number];
        line_metrics[line_number].boxes.emplace_back(
            SkRect::MakeLTRB(x, top, x, bottom), TextDirection::ltr);
      }
    }
  }

  // "Post-process" metrics and aggregate final rects to return.
  std::vector<Paragraph::TextBox> boxes;
  for (const auto& kv : line_metrics) {
    // Handle rect_width_styles. We skip the last line because not everything is
    // selected.
    if (rect_width_style == RectWidthStyle::kMax && kv.first != max_line) {
      if (line_metrics[kv.first].min_left > min_left_ &&
          (kv.first != min_line || first_line_dir == TextDirection::rtl)) {
        line_metrics[kv.first].boxes.emplace_back(
            SkRect::MakeLTRB(
                min_left_,
                line_baselines_[kv.first] - line_max_ascent_[kv.first],
                line_metrics[kv.first].min_left,
                line_baselines_[kv.first] + line_max_descent_[kv.first]),
            TextDirection::rtl);
      }
      if (line_metrics[kv.first].max_right < max_right_ &&
          (kv.first != min_line || first_line_dir == TextDirection::ltr)) {
        line_metrics[kv.first].boxes.emplace_back(
            SkRect::MakeLTRB(
                line_metrics[kv.first].max_right,
                line_baselines_[kv.first] - line_max_ascent_[kv.first],
                max_right_,
                line_baselines_[kv.first] + line_max_descent_[kv.first]),
            TextDirection::ltr);
      }
    }

    // Handle rect_height_styles. The height metrics used are all positive to
    // make the signage clear here.
    if (rect_height_style == RectHeightStyle::kTight) {
      // Ignore line max height and width and generate tight bounds.
      boxes.insert(boxes.end(), kv.second.boxes.begin(), kv.second.boxes.end());
    } else if (rect_height_style == RectHeightStyle::kMax) {
      for (const Paragraph::TextBox& box : kv.second.boxes) {
        boxes.emplace_back(
            SkRect::MakeLTRB(
                box.rect.fLeft,
                line_baselines_[kv.first] - line_max_ascent_[kv.first],
                box.rect.fRight,
                line_baselines_[kv.first] + line_max_descent_[kv.first]),
            box.direction);
      }
    } else if (rect_height_style ==
               RectHeightStyle::kIncludeLineSpacingMiddle) {
      SkScalar adjusted_bottom =
          line_baselines_[kv.first] + line_max_descent_[kv.first];
      if (kv.first < line_ranges_.size() - 1) {
        adjusted_bottom += (line_max_spacings_[kv.first + 1] -
                            line_max_ascent_[kv.first + 1]) /
                           2;
      }
      SkScalar adjusted_top =
          line_baselines_[kv.first] - line_max_ascent_[kv.first];
      if (kv.first != 0) {
        adjusted_top -=
            (line_max_spacings_[kv.first] - line_max_ascent_[kv.first]) / 2;
      }
      for (const Paragraph::TextBox& box : kv.second.boxes) {
        boxes.emplace_back(SkRect::MakeLTRB(box.rect.fLeft, adjusted_top,
                                            box.rect.fRight, adjusted_bottom),
                           box.direction);
      }
    } else if (rect_height_style == RectHeightStyle::kIncludeLineSpacingTop) {
      for (const Paragraph::TextBox& box : kv.second.boxes) {
        SkScalar adjusted_top =
            kv.first == 0
                ? line_baselines_[kv.first] - line_max_ascent_[kv.first]
                : line_baselines_[kv.first] - line_max_spacings_[kv.first];
        boxes.emplace_back(
            SkRect::MakeLTRB(
                box.rect.fLeft, adjusted_top, box.rect.fRight,
                line_baselines_[kv.first] + line_max_descent_[kv.first]),
            box.direction);
      }
    } else if (rect_height_style ==
               RectHeightStyle::kIncludeLineSpacingBottom) {
      for (const Paragraph::TextBox& box : kv.second.boxes) {
        SkScalar adjusted_bottom =
            line_baselines_[kv.first] + line_max_descent_[kv.first];
        if (kv.first < line_ranges_.size() - 1) {
          adjusted_bottom +=
              -line_max_ascent_[kv.first] + line_max_spacings_[kv.first];
        }
        boxes.emplace_back(SkRect::MakeLTRB(box.rect.fLeft,
                                            line_baselines_[kv.first] -
                                                line_max_ascent_[kv.first],
                                            box.rect.fRight, adjusted_bottom),
                           box.direction);
      }
    } else if (rect_height_style == RectHeightStyle::kStrut) {
      if (IsStrutValid()) {
        for (const Paragraph::TextBox& box : kv.second.boxes) {
          boxes.emplace_back(
              SkRect::MakeLTRB(
                  box.rect.fLeft, line_baselines_[kv.first] - strut_.ascent,
                  box.rect.fRight, line_baselines_[kv.first] + strut_.descent),
              box.direction);
        }
      } else {
        // Fall back to tight bounds if the strut is invalid.
        boxes.insert(boxes.end(), kv.second.boxes.begin(),
                     kv.second.boxes.end());
      }
    }
  }
  return boxes;
}

Paragraph::PositionWithAffinity Paragraph::GetGlyphPositionAtCoordinate(
    double dx,
    double dy) const {
  if (line_heights_.empty())
    return PositionWithAffinity(0, DOWNSTREAM);

  size_t y_index;
  for (y_index = 0; y_index < line_heights_.size() - 1; ++y_index) {
    if (dy < line_heights_[y_index])
      break;
  }

  const std::vector<GlyphPosition>& line_glyph_position =
      glyph_lines_[y_index].positions;
  if (line_glyph_position.empty()) {
    int line_start_index =
        std::accumulate(glyph_lines_.begin(), glyph_lines_.begin() + y_index, 0,
                        [](const int a, const GlyphLine& b) {
                          return a + static_cast<int>(b.total_code_units);
                        });
    return PositionWithAffinity(line_start_index, DOWNSTREAM);
  }

  size_t x_index;
  const GlyphPosition* gp = nullptr;
  for (x_index = 0; x_index < line_glyph_position.size(); ++x_index) {
    double glyph_end = (x_index < line_glyph_position.size() - 1)
                           ? line_glyph_position[x_index + 1].x_pos.start
                           : line_glyph_position[x_index].x_pos.end;
    if (dx < glyph_end) {
      gp = &line_glyph_position[x_index];
      break;
    }
  }

  if (gp == nullptr) {
    const GlyphPosition& last_glyph = line_glyph_position.back();
    return PositionWithAffinity(last_glyph.code_units.end, UPSTREAM);
  }

  // Find the direction of the run that contains this glyph.
  TextDirection direction = TextDirection::ltr;
  for (const CodeUnitRun& run : code_unit_runs_) {
    if (gp->code_units.start >= run.code_units.start &&
        gp->code_units.end <= run.code_units.end) {
      direction = run.direction;
      break;
    }
  }

  double glyph_center = (gp->x_pos.start + gp->x_pos.end) / 2;
  if ((direction == TextDirection::ltr && dx < glyph_center) ||
      (direction == TextDirection::rtl && dx >= glyph_center)) {
    return PositionWithAffinity(gp->code_units.start, DOWNSTREAM);
  } else {
    return PositionWithAffinity(gp->code_units.end, UPSTREAM);
  }
}

// We don't cache this because since this returns all boxes, it is usually
// unnecessary to call this multiple times in succession.
std::vector<Paragraph::TextBox> Paragraph::GetRectsForPlaceholders() const {
  // Struct that holds calculated metrics for each line.
  struct LineBoxMetrics {
    std::vector<Paragraph::TextBox> boxes;
    // Per-line metrics for max and min coordinates for left and right boxes.
    // These metrics cannot be calculated in layout generically because of
    // selections that do not cover the whole line.
    SkScalar max_right = FLT_MIN;
    SkScalar min_left = FLT_MAX;
  };

  std::vector<Paragraph::TextBox> boxes;

  // Generate initial boxes and calculate metrics.
  for (const CodeUnitRun& run : inline_placeholder_code_unit_runs_) {
    // Check to see if we are finished.
    double baseline = line_baselines_[run.line_number];
    SkScalar top = baseline + run.font_metrics.fAscent;
    SkScalar bottom = baseline + run.font_metrics.fDescent;

    if (run.placeholder_run !=
        nullptr) {  // Use inline placeholder size as height.
      top = baseline - run.placeholder_run->baseline_offset;
      bottom = baseline + run.placeholder_run->height -
               run.placeholder_run->baseline_offset;
    }

    // Calculate left and right.
    SkScalar left, right;
    left = run.x_pos.start;
    right = run.x_pos.end;

    boxes.emplace_back(SkRect::MakeLTRB(left, top, right, bottom),
                       run.direction);
  }
  return boxes;
}

Paragraph::Range<size_t> Paragraph::GetWordBoundary(size_t offset) const {
  if (text_.size() == 0)
    return Range<size_t>(0, 0);

  if (!word_breaker_) {
    UErrorCode status = U_ZERO_ERROR;
    word_breaker_.reset(
        icu::BreakIterator::createWordInstance(icu::Locale(), status));
    if (!U_SUCCESS(status))
      return Range<size_t>(0, 0);
  }

  word_breaker_->setText(icu::UnicodeString(false, text_.data(), text_.size()));

  int32_t prev_boundary = word_breaker_->preceding(offset + 1);
  int32_t next_boundary = word_breaker_->next();
  if (prev_boundary == icu::BreakIterator::DONE)
    prev_boundary = offset;
  if (next_boundary == icu::BreakIterator::DONE)
    next_boundary = offset;
  return Range<size_t>(prev_boundary, next_boundary);
}

size_t Paragraph::GetLineCount() const {
  return line_heights_.size();
}

bool Paragraph::DidExceedMaxLines() const {
  return did_exceed_max_lines_;
}

void Paragraph::SetDirty(bool dirty) {
  needs_layout_ = dirty;
}

}  // namespace txt
