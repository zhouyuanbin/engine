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

#ifndef LIB_TXT_SRC_PARAGRAPH_BUILDER_H_
#define LIB_TXT_SRC_PARAGRAPH_BUILDER_H_

#include <memory>
#include <string>

#include "flutter/fml/macros.h"
#include "font_collection.h"
#include "paragraph.h"
#include "paragraph_style.h"
#include "placeholder_run.h"
#include "styled_runs.h"
#include "text_style.h"

namespace txt {

class ParagraphBuilder {
 public:
  ParagraphBuilder(ParagraphStyle style,
                   std::shared_ptr<FontCollection> font_collection);

  ~ParagraphBuilder();

  // Push a style to the stack. The corresponding text added with AddText will
  // use the top-most style.
  void PushStyle(const TextStyle& style);

  // Remove a style from the stack. Useful to apply different styles to chunks
  // of text such as bolding.
  // Example:
  //   builder.PushStyle(normal_style);
  //   builder.AddText("Hello this is normal. ");
  //
  //   builder.PushStyle(bold_style);
  //   builder.AddText("And this is BOLD. ");
  //
  //   builder.Pop();
  //   builder.AddText(" Back to normal again.");
  void Pop();

  // Returns the last TextStyle on the stack.
  const TextStyle& PeekStyle() const;

  // Adds text to the builder. Forms the proper runs to use the upper-most style
  // on the style_stack_;
  void AddText(const std::u16string& text);

  // Converts to u16string before adding.
  void AddText(const std::string& text);

  // Converts to u16string before adding.
  void AddText(const char* text);

  // Pushes the information requried to leave an open space, where Flutter may
  // draw a custom placeholder into.
  //
  // Internally, this method adds a single object replacement character (0xFFFC)
  // and emplaces a new PlaceholderRun instance to the vector of inline
  // placeholders.
  void AddPlaceholder(PlaceholderRun& span);

  void SetParagraphStyle(const ParagraphStyle& style);

  // Constructs a Paragraph object that can be used to layout and paint the text
  // to a SkCanvas.
  std::unique_ptr<Paragraph> Build();

 private:
  std::vector<uint16_t> text_;
  // A vector of PlaceholderRuns, which detail the sizes, positioning and break
  // behavior of the empty spaces to leave. Each placeholder span corresponds to
  // a 0xFFFC (object replacement character) in text_, which indicates the
  // position in the text where the placeholder will occur. There should be an
  // equal number of 0xFFFC characters and elements in this vector.
  std::vector<PlaceholderRun> inline_placeholders_;
  // The indexes of the obj replacement characters added through
  // ParagraphBuilder::addPlaceholder().
  std::unordered_set<size_t> obj_replacement_char_indexes_;
  std::vector<size_t> style_stack_;
  std::shared_ptr<FontCollection> font_collection_;
  StyledRuns runs_;
  ParagraphStyle paragraph_style_;
  size_t paragraph_style_index_;

  size_t PeekStyleIndex() const;

  FML_DISALLOW_COPY_AND_ASSIGN(ParagraphBuilder);
};

}  // namespace txt

#endif  // LIB_TXT_SRC_PARAGRAPH_BUILDER_H_
