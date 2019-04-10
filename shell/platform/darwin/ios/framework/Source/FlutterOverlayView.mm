// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/darwin/ios/framework/Source/FlutterOverlayView.h"

#include "flutter/common/settings.h"
#include "flutter/common/task_runners.h"
#include "flutter/flow/layers/layer_tree.h"
#include "flutter/fml/platform/darwin/cf_utils.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/fml/trace_event.h"
#include "flutter/shell/common/platform_view.h"
#include "flutter/shell/common/rasterizer.h"
#include "flutter/shell/platform/darwin/ios/ios_surface_gl.h"
#include "flutter/shell/platform/darwin/ios/ios_surface_software.h"
#include "third_party/skia/include/utils/mac/SkCGUtils.h"

// This is mostly a duplication of FlutterView.
// TODO(amirh): once GL support is in evaluate if we can merge this with FlutterView.
@implementation FlutterOverlayView

- (instancetype)initWithFrame:(CGRect)frame {
  @throw([NSException exceptionWithName:@"FlutterOverlayView must init or initWithContentsScale"
                                 reason:nil
                               userInfo:nil]);
}

- (instancetype)initWithCoder:(NSCoder*)aDecoder {
  @throw([NSException exceptionWithName:@"FlutterOverlayView must init or initWithContentsScale"
                                 reason:nil
                               userInfo:nil]);
}

- (instancetype)init {
  self = [super initWithFrame:CGRectZero];

  if (self) {
    self.layer.opaque = NO;
    self.userInteractionEnabled = NO;
  }

  return self;
}

- (instancetype)initWithContentsScale:(CGFloat)contentsScale {
  self = [self init];

  if ([self.layer isKindOfClass:[CAEAGLLayer class]]) {
    CAEAGLLayer* layer = reinterpret_cast<CAEAGLLayer*>(self.layer);
    layer.allowsGroupOpacity = NO;
    layer.contentsScale = contentsScale;
    layer.rasterizationScale = contentsScale;
  }

  return self;
}

+ (Class)layerClass {
#if TARGET_IPHONE_SIMULATOR
  return [CALayer class];
#else   // TARGET_IPHONE_SIMULATOR
  return [CAEAGLLayer class];
#endif  // TARGET_IPHONE_SIMULATOR
}

- (std::unique_ptr<flutter::IOSSurface>)createSoftwareSurface {
  fml::scoped_nsobject<CALayer> layer(reinterpret_cast<CALayer*>([self.layer retain]));
  return std::make_unique<flutter::IOSSurfaceSoftware>(std::move(layer), nullptr);
}

- (std::unique_ptr<flutter::IOSSurfaceGL>)createGLSurfaceWithContext:
    (std::shared_ptr<flutter::IOSGLContext>)gl_context {
  fml::scoped_nsobject<CAEAGLLayer> eagl_layer(reinterpret_cast<CAEAGLLayer*>([self.layer retain]));
  // TODO(amirh): We can lower this to iOS 8.0 once we have a Metal rendering backend.
  // https://github.com/flutter/flutter/issues/24132
  if (@available(iOS 9.0, *)) {
    eagl_layer.get().presentsWithTransaction = YES;
  }
  return std::make_unique<flutter::IOSSurfaceGL>(eagl_layer, std::move(gl_context));
}

// TODO(amirh): implement drawLayer to suppoer snapshotting.

@end
