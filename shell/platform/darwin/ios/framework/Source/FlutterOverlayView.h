// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHELL_PLATFORM_IOS_FRAMEWORK_SOURCE_FLUTTER_OVERLAY_VIEW_H_
#define SHELL_PLATFORM_IOS_FRAMEWORK_SOURCE_FLUTTER_OVERLAY_VIEW_H_

#include <UIKit/UIKit.h>

#include <memory>

#import "FlutterPlatformViews_Internal.h"

#include "flutter/fml/memory/weak_ptr.h"
#include "flutter/shell/common/shell.h"
#include "flutter/shell/platform/darwin/ios/ios_gl_context.h"
#include "flutter/shell/platform/darwin/ios/ios_surface.h"
#include "flutter/shell/platform/darwin/ios/ios_surface_gl.h"

@interface FlutterOverlayView : UIView

- (instancetype)initWithFrame:(CGRect)frame NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder*)aDecoder NS_UNAVAILABLE;

- (instancetype)init NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithContentsScale:(CGFloat)contentsScale;
- (std::unique_ptr<flutter::IOSSurface>)createSurface:
    (std::shared_ptr<flutter::IOSGLContext>)gl_context;

@end

#endif  // SHELL_PLATFORM_IOS_FRAMEWORK_SOURCE_FLUTTER_OVERLAY_VIEW_H_
