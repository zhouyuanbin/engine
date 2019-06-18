// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FLOW_LAYERS_COLOR_FILTER_LAYER_H_
#define FLUTTER_FLOW_LAYERS_COLOR_FILTER_LAYER_H_

#include "flutter/flow/layers/container_layer.h"

namespace flutter {

class ColorFilterLayer : public ContainerLayer {
 public:
  ColorFilterLayer(SkColor color, SkBlendMode blend_mode);
  ~ColorFilterLayer() override;

  void Paint(PaintContext& context) const override;

 private:
  SkColor color_;
  SkBlendMode blend_mode_;

  FML_DISALLOW_COPY_AND_ASSIGN(ColorFilterLayer);
};

}  // namespace flutter

#endif  // FLUTTER_FLOW_LAYERS_COLOR_FILTER_LAYER_H_
