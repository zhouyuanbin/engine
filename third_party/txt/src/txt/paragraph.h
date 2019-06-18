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

#ifndef LIB_TXT_SRC_PARAGRAPH_H_
#define LIB_TXT_SRC_PARAGRAPH_H_

#include <set>
#include <utility>
#include <vector>

#include "flutter/fml/compiler_specific.h"
#include "flutter/fml/macros.h"
#include "font_collection.h"
#include "minikin/LineBreaker.h"
#include "paint_record.h"
#include "paragraph_style.h"
#include "placeholder_run.h"
#include "styled_runs.h"
#include "third_party/googletest/googletest/include/gtest/gtest_prod.h"  // nogncheck
#include "third_party/skia/include/core/SkFontMetrics.h"
#include "third_party/skia/include/core/SkRect.h"
#include "utils/WindowsUtils.h"

class SkCanvas;

namespace txt {

using GlyphID = uint32_t;

// Constant with the unicode codepoint for the "Object replacement character".
// Used as a stand-in character for Placeholder boxes.
const int objReplacementChar = 0xFFFC;
// Constant with the unicode codepoint for the "Replacement character". This is
// the character that commonly renders as a black diamond with a white question
// mark. Used to replace non-placeholder instances of 0xFFFC in the text buffer.
const int replacementChar = 0xFFFD;

// Paragraph provides Layout, metrics, and painting capabilities for text. Once
// a Paragraph is constructed with ParagraphBuilder::Build(), an example basic
// workflow can be this:
//
//   std::unique_ptr<Paragraph> paragraph = paragraph_builder.Build();
//   paragraph->Layout(<somewidthgoeshere>);
//   paragraph->Paint(<someSkCanvas>, <xpos>, <ypos>);
class Paragraph {
 public:
  // Constructor. It is highly recommended to construct a paragraph with a
  // ParagraphBuilder.
  Paragraph();

  ~Paragraph();

  enum Affinity { UPSTREAM, DOWNSTREAM };

  // Options for various types of bounding boxes provided by
  // GetRectsForRange(...).
  enum class RectHeightStyle {
    // Provide tight bounding boxes that fit heights per run.
    kTight,

    // The height of the boxes will be the maximum height of all runs in the
    // line. All rects in the same line will be the same height.
    kMax,

    // Extends the top and/or bottom edge of the bounds to fully cover any line
    // spacing. The top edge of each line should be the same as the bottom edge
    // of the line above. There should be no gaps in vertical coverage given any
    // ParagraphStyle line_height.
    //
    // The top and bottom of each rect will cover half of the
    // space above and half of the space below the line.
    kIncludeLineSpacingMiddle,
    // The line spacing will be added to the top of the rect.
    kIncludeLineSpacingTop,
    // The line spacing will be added to the bottom of the rect.
    kIncludeLineSpacingBottom,

    // Calculate boxes based on the strut's metrics.
    kStrut
  };

  enum class RectWidthStyle {
    // Provide tight bounding boxes that fit widths to the runs of each line
    // independently.
    kTight,

    // Extends the width of the last rect of each line to match the position of
    // the widest rect over all the lines.
    kMax
  };

  struct PositionWithAffinity {
    const size_t position;
    const Affinity affinity;

    PositionWithAffinity(size_t p, Affinity a) : position(p), affinity(a) {}
  };

  struct TextBox {
    SkRect rect;
    TextDirection direction;

    TextBox(SkRect r, TextDirection d) : rect(r), direction(d) {}
  };

  template <typename T>
  struct Range {
    Range() : start(), end() {}
    Range(T s, T e) : start(s), end(e) {}

    T start, end;

    bool operator==(const Range<T>& other) const {
      return start == other.start && end == other.end;
    }

    T width() const { return end - start; }

    void Shift(T delta) {
      start += delta;
      end += delta;
    }
  };

  // Minikin Layout doLayout() and LineBreaker addStyleRun() has an
  // O(N^2) (according to benchmarks) time complexity where N is the total
  // number of characters. However, this is not significant for reasonably sized
  // paragraphs. It is currently recommended to break up very long paragraphs
  // (10k+ characters) to ensure speedy layout.
  //
  // Layout calculates the positioning of all the glyphs. Must call this method
  // before Painting and getting any statistics from this class.
  void Layout(double width, bool force = false);

  // Paints the Laid out text onto the supplied SkCanvas at (x, y) offset from
  // the origin. Only valid after Layout() is called.
  void Paint(SkCanvas* canvas, double x, double y);

  // Getter for paragraph_style_.
  const ParagraphStyle& GetParagraphStyle() const;

  // Returns the number of characters/unicode characters. AKA text_.size()
  size_t TextSize() const;

  // Returns the height of the laid out paragraph. NOTE this is not a tight
  // bounding height of the glyphs, as some glyphs do not reach as low as they
  // can.
  double GetHeight() const;

  // Returns the width provided in the Layout() method. This is the maximum
  // width any line in the laid out paragraph can occupy. We expect that
  // GetMaxWidth() >= GetLayoutWidth().
  double GetMaxWidth() const;

  // Returns the width of the longest line as found in Layout(), which is
  // defined as the horizontal distance from the left edge of the leftmost glyph
  // to the right edge of the rightmost glyph. We expect that
  // GetLongestLine() <= GetMaxWidth().
  double GetLongestLine() const;

  // Distance from top of paragraph to the Alphabetic baseline of the first
  // line. Used for alphabetic fonts (A-Z, a-z, greek, etc.)
  double GetAlphabeticBaseline() const;

  // Distance from top of paragraph to the Ideographic baseline of the first
  // line. Used for ideographic fonts (Chinese, Japanese, Korean, etc.)
  double GetIdeographicBaseline() const;

  // Returns the total width covered by the paragraph without linebreaking.
  double GetMaxIntrinsicWidth() const;

  // Currently, calculated similarly to as GetLayoutWidth(), however this is not
  // nessecarily 100% correct in all cases.
  //
  // Returns the actual max width of the longest line after Layout().
  double GetMinIntrinsicWidth() const;

  // Returns a vector of bounding boxes that enclose all text between start and
  // end glyph indexes, including start and excluding end.
  std::vector<TextBox> GetRectsForRange(size_t start,
                                        size_t end,
                                        RectHeightStyle rect_height_style,
                                        RectWidthStyle rect_width_style) const;

  // Returns the index of the glyph that corresponds to the provided coordinate,
  // with the top left corner as the origin, and +y direction as down.
  PositionWithAffinity GetGlyphPositionAtCoordinate(double dx, double dy) const;

  // Returns a vector of bounding boxes that bound all inline placeholders in
  // the paragraph.
  //
  // There will be one box for each inline placeholder. The boxes will be in the
  // same order as they were added to the paragraph. The bounds will always be
  // tight and should fully enclose the area where the placeholder should be.
  //
  // More granular boxes may be obtained through GetRectsForRange, which will
  // return bounds on both text as well as inline placeholders.
  std::vector<Paragraph::TextBox> GetRectsForPlaceholders() const;

  // Finds the first and last glyphs that define a word containing the glyph at
  // index offset.
  Range<size_t> GetWordBoundary(size_t offset) const;

  // Returns the number of lines the paragraph takes up. If the text exceeds the
  // amount width and maxlines provides, Layout() truncates the extra text from
  // the layout and this will return the max lines allowed.
  size_t GetLineCount() const;

  // Checks if the layout extends past the maximum lines and had to be
  // truncated.
  bool DidExceedMaxLines() const;

  // Sets the needs_layout_ to dirty. When Layout() is called, a new Layout will
  // be performed when this is set to true. Can also be used to prevent a new
  // Layout from being calculated by setting to false.
  void SetDirty(bool dirty = true);

 private:
  friend class ParagraphBuilder;
  FRIEND_TEST(ParagraphTest, SimpleParagraph);
  FRIEND_TEST(ParagraphTest, SimpleRedParagraph);
  FRIEND_TEST(ParagraphTest, RainbowParagraph);
  FRIEND_TEST(ParagraphTest, DefaultStyleParagraph);
  FRIEND_TEST(ParagraphTest, BoldParagraph);
  FRIEND_TEST_WINDOWS_DISABLED(ParagraphTest, LeftAlignParagraph);
  FRIEND_TEST_WINDOWS_DISABLED(ParagraphTest, RightAlignParagraph);
  FRIEND_TEST_WINDOWS_DISABLED(ParagraphTest, CenterAlignParagraph);
  FRIEND_TEST_WINDOWS_DISABLED(ParagraphTest, JustifyAlignParagraph);
  FRIEND_TEST_WINDOWS_DISABLED(ParagraphTest, JustifyRTL);
  FRIEND_TEST(ParagraphTest, DecorationsParagraph);
  FRIEND_TEST(ParagraphTest, ItalicsParagraph);
  FRIEND_TEST(ParagraphTest, ChineseParagraph);
  FRIEND_TEST(ParagraphTest, DISABLED_ArabicParagraph);
  FRIEND_TEST(ParagraphTest, SpacingParagraph);
  FRIEND_TEST(ParagraphTest, LongWordParagraph);
  FRIEND_TEST(ParagraphTest, KernScaleParagraph);
  FRIEND_TEST_WINDOWS_DISABLED(ParagraphTest, NewlineParagraph);
  FRIEND_TEST_WINDOWS_DISABLED(ParagraphTest, EmojiParagraph);
  FRIEND_TEST(ParagraphTest, HyphenBreakParagraph);
  FRIEND_TEST(ParagraphTest, RepeatLayoutParagraph);
  FRIEND_TEST(ParagraphTest, Ellipsize);
  FRIEND_TEST(ParagraphTest, UnderlineShiftParagraph);
  FRIEND_TEST(ParagraphTest, SimpleShadow);
  FRIEND_TEST(ParagraphTest, ComplexShadow);
  FRIEND_TEST(ParagraphTest, FontFallbackParagraph);
  FRIEND_TEST(ParagraphTest, InlinePlaceholder0xFFFCParagraph);
  FRIEND_TEST(ParagraphTest, FontFeaturesParagraph);

  // Starting data to layout.
  std::vector<uint16_t> text_;
  // A vector of PlaceholderRuns, which detail the sizes, positioning and break
  // behavior of the empty spaces to leave. Each placeholder span corresponds to
  // a 0xFFFC (object replacement character) in text_, which indicates the
  // position in the text where the placeholder will occur. There should be an
  // equal number of 0xFFFC characters and elements in this vector.
  std::vector<PlaceholderRun> inline_placeholders_;
  // The indexes of the boxes that correspond to an inline placeholder.
  std::vector<size_t> inline_placeholder_boxes_;
  // The indexes of instances of 0xFFFC that correspond to placeholders. This is
  // necessary since the user may pass in manually entered 0xFFFC values using
  // AddText().
  std::unordered_set<size_t> obj_replacement_char_indexes_;
  StyledRuns runs_;
  ParagraphStyle paragraph_style_;
  std::shared_ptr<FontCollection> font_collection_;

  minikin::LineBreaker breaker_;
  mutable std::unique_ptr<icu::BreakIterator> word_breaker_;

  struct LineRange {
    LineRange(size_t s, size_t e, size_t eew, size_t ein, bool h)
        : start(s),
          end(e),
          end_excluding_whitespace(eew),
          end_including_newline(ein),
          hard_break(h) {}
    size_t start, end;
    size_t end_excluding_whitespace;
    size_t end_including_newline;
    bool hard_break;
  };
  std::vector<LineRange> line_ranges_;
  std::vector<double> line_widths_;

  // Stores the result of Layout().
  std::vector<PaintRecord> records_;

  std::vector<double> line_heights_;
  std::vector<double> line_baselines_;
  bool did_exceed_max_lines_;

  // Strut metrics of zero will have no effect on the layout.
  struct StrutMetrics {
    double ascent = 0;  // Positive value to keep signs clear.
    double descent = 0;
    double leading = 0;
    double half_leading = 0;
    double line_height = 0;
    bool force_strut = false;
  };

  StrutMetrics strut_;

  // Metrics for use in GetRectsForRange(...);
  // Per-line max metrics over all runs in a given line.
  std::vector<SkScalar> line_max_spacings_;
  std::vector<SkScalar> line_max_descent_;
  std::vector<SkScalar> line_max_ascent_;
  // Overall left and right extremes over all lines.
  double max_right_;
  double min_left_;

  class BidiRun {
   public:
    // Constructs a BidiRun with is_ghost defaulted to false.
    BidiRun(size_t s, size_t e, TextDirection d, const TextStyle& st)
        : start_(s), end_(e), direction_(d), style_(&st), is_ghost_(false) {}

    // Constructs a BidiRun with a custom is_ghost flag.
    BidiRun(size_t s,
            size_t e,
            TextDirection d,
            const TextStyle& st,
            bool is_ghost)
        : start_(s), end_(e), direction_(d), style_(&st), is_ghost_(is_ghost) {}

    // Constructs a placeholder bidi run.
    BidiRun(size_t s,
            size_t e,
            TextDirection d,
            const TextStyle& st,
            PlaceholderRun& placeholder)
        : start_(s),
          end_(e),
          direction_(d),
          style_(&st),
          placeholder_run_(&placeholder) {}

    size_t start() const { return start_; }
    size_t end() const { return end_; }
    size_t size() const { return end_ - start_; }
    TextDirection direction() const { return direction_; }
    const TextStyle& style() const { return *style_; }
    PlaceholderRun* placeholder_run() const { return placeholder_run_; }
    bool is_rtl() const { return direction_ == TextDirection::rtl; }
    // Tracks if the run represents trailing whitespace.
    bool is_ghost() const { return is_ghost_; }
    bool is_placeholder_run() const { return placeholder_run_ != nullptr; }

   private:
    size_t start_, end_;
    TextDirection direction_;
    const TextStyle* style_;
    bool is_ghost_;
    PlaceholderRun* placeholder_run_ = nullptr;
  };

  struct GlyphPosition {
    Range<size_t> code_units;
    Range<double> x_pos;

    GlyphPosition(double x_start,
                  double x_advance,
                  size_t code_unit_index,
                  size_t code_unit_width);

    void Shift(double delta);
  };

  struct GlyphLine {
    // Glyph positions sorted by x coordinate.
    const std::vector<GlyphPosition> positions;
    const size_t total_code_units;

    GlyphLine(std::vector<GlyphPosition>&& p, size_t tcu);
  };

  struct CodeUnitRun {
    // Glyph positions sorted by code unit index.
    std::vector<GlyphPosition> positions;
    Range<size_t> code_units;
    Range<double> x_pos;
    size_t line_number;
    SkFontMetrics font_metrics;
    TextDirection direction;
    const PlaceholderRun* placeholder_run;

    CodeUnitRun(std::vector<GlyphPosition>&& p,
                Range<size_t> cu,
                Range<double> x,
                size_t line,
                const SkFontMetrics& metrics,
                TextDirection dir,
                const PlaceholderRun* placeholder);

    void Shift(double delta);
  };

  // Holds the laid out x positions of each glyph.
  std::vector<GlyphLine> glyph_lines_;

  // Holds the positions of each range of code units in the text.
  // Sorted in code unit index order.
  std::vector<CodeUnitRun> code_unit_runs_;
  // Holds the positions of the inline placeholders.
  std::vector<CodeUnitRun> inline_placeholder_code_unit_runs_;

  // The max width of the paragraph as provided in the most recent Layout()
  // call.
  double width_ = -1.0f;
  double longest_line_ = -1.0f;
  double max_intrinsic_width_ = 0;
  double min_intrinsic_width_ = 0;
  double alphabetic_baseline_ = FLT_MAX;
  double ideographic_baseline_ = FLT_MAX;

  bool needs_layout_ = true;

  struct WaveCoordinates {
    double x_start;
    double y_start;
    double x_end;
    double y_end;

    WaveCoordinates(double x_s, double y_s, double x_e, double y_e)
        : x_start(x_s), y_start(y_s), x_end(x_e), y_end(y_e) {}
  };

  // Passes in the text and Styled Runs. text_ and runs_ will later be passed
  // into breaker_ in InitBreaker(), which is called in Layout().
  void SetText(std::vector<uint16_t> text, StyledRuns runs);

  void SetParagraphStyle(const ParagraphStyle& style);

  void SetFontCollection(std::shared_ptr<FontCollection> font_collection);

  void SetInlinePlaceholders(
      std::vector<PlaceholderRun> inline_placeholders,
      std::unordered_set<size_t> obj_replacement_char_indexes);

  // Break the text into lines.
  bool ComputeLineBreaks();

  // Break the text into runs based on LTR/RTL text direction.
  bool ComputeBidiRuns(std::vector<BidiRun>* result);

  // Calculates and populates strut based on paragraph_style_ strut info.
  void ComputeStrut(StrutMetrics* strut, SkFont& font);

  // Adjusts the ascent and descent based on the existence and type of
  // placeholder. This method sets the proper metrics to achieve the different
  // PlaceholderAlignment options.
  void ComputePlaceholder(PlaceholderRun* placeholder_run,
                          double& ascent,
                          double& descent);

  bool IsStrutValid() const;

  // Calculate the starting X offset of a line based on the line's width and
  // alignment.
  double GetLineXOffset(double line_total_advance);

  // Creates and draws the decorations onto the canvas.
  void PaintDecorations(SkCanvas* canvas,
                        const PaintRecord& record,
                        SkPoint base_offset);

  // Draws the background onto the canvas.
  void PaintBackground(SkCanvas* canvas,
                       const PaintRecord& record,
                       SkPoint base_offset);

  // Draws the shadows onto the canvas.
  void PaintShadow(SkCanvas* canvas, const PaintRecord& record, SkPoint offset);

  // Obtain a Minikin font collection matching this text style.
  std::shared_ptr<minikin::FontCollection> GetMinikinFontCollectionForStyle(
      const TextStyle& style);

  // Get a default SkTypeface for a text style.
  sk_sp<SkTypeface> GetDefaultSkiaTypeface(const TextStyle& style);

  FML_DISALLOW_COPY_AND_ASSIGN(Paragraph);
};

}  // namespace txt

#endif  // LIB_TXT_SRC_PARAGRAPH_H_
