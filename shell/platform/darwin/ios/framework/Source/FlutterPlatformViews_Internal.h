// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_DARWIN_IOS_FRAMEWORK_SOURCE_FLUTTERPLATFORMVIEWS_INTERNAL_H_
#define FLUTTER_SHELL_PLATFORM_DARWIN_IOS_FRAMEWORK_SOURCE_FLUTTERPLATFORMVIEWS_INTERNAL_H_

#include "flutter/flow/embedded_views.h"
#include "flutter/fml/platform/darwin/scoped_nsobject.h"
#include "flutter/shell/common/shell.h"
#include "flutter/shell/platform/darwin/common/framework/Headers/FlutterBinaryMessenger.h"
#include "flutter/shell/platform/darwin/common/framework/Headers/FlutterChannels.h"
#include "flutter/shell/platform/darwin/ios/framework/Headers/FlutterPlatformViews.h"

// A UIView that is used as the parent for embedded UIViews.
//
// This view has 2 roles:
// 1. Delay or prevent touch events from arriving the embedded view.
// 2. Dispatching all events that are hittested to the embedded view to the FlutterView.
@interface FlutterTouchInterceptingView : UIView
- (instancetype)initWithEmbeddedView:(UIView*)embeddedView
               flutterViewController:(UIViewController*)flutterViewController;

// Stop delaying any active touch sequence (and let it arrive the embedded view).
- (void)releaseGesture;

// Prevent the touch sequence from ever arriving to the embedded view.
- (void)blockGesture;
@end

namespace flutter {

class IOSGLContext;
class IOSSurface;

struct FlutterPlatformViewLayer {
  FlutterPlatformViewLayer(fml::scoped_nsobject<UIView> overlay_view,
                           std::unique_ptr<IOSSurface> ios_surface,
                           std::unique_ptr<Surface> surface);

  ~FlutterPlatformViewLayer();

  fml::scoped_nsobject<UIView> overlay_view;
  std::unique_ptr<IOSSurface> ios_surface;
  std::unique_ptr<Surface> surface;
};

class FlutterPlatformViewsController {
 public:
  FlutterPlatformViewsController();

  ~FlutterPlatformViewsController();

  void SetFlutterView(UIView* flutter_view);

  void SetFlutterViewController(UIViewController* flutter_view_controller);

  void RegisterViewFactory(NSObject<FlutterPlatformViewFactory>* factory, NSString* factoryId);

  void SetFrameSize(SkISize frame_size);

  void PrerollCompositeEmbeddedView(int view_id);

  // Returns the `FlutterPlatformView` object associated with the view_id.
  //
  // If the `FlutterPlatformViewsController` does not contain any `FlutterPlatformView` object or
  // a `FlutterPlatformView` object asscociated with the view_id cannot be found, the method returns
  // nil.
  NSObject<FlutterPlatformView>* GetPlatformViewByID(int view_id);

  std::vector<SkCanvas*> GetCurrentCanvases();

  SkCanvas* CompositeEmbeddedView(int view_id, const flutter::EmbeddedViewParams& params);

  // Discards all platform views instances and auxiliary resources.
  void Reset();

  bool SubmitFrame(bool gl_rendering,
                   GrContext* gr_context,
                   std::shared_ptr<IOSGLContext> gl_context);

  void OnMethodCall(FlutterMethodCall* call, FlutterResult& result);

 private:
  fml::scoped_nsobject<FlutterMethodChannel> channel_;
  fml::scoped_nsobject<UIView> flutter_view_;
  fml::scoped_nsobject<UIViewController> flutter_view_controller_;
  std::map<std::string, fml::scoped_nsobject<NSObject<FlutterPlatformViewFactory>>> factories_;
  std::map<int64_t, fml::scoped_nsobject<NSObject<FlutterPlatformView>>> views_;
  std::map<int64_t, fml::scoped_nsobject<FlutterTouchInterceptingView>> touch_interceptors_;
  // Mapping a platform view ID to its latest composition params.
  std::map<int64_t, EmbeddedViewParams> current_composition_params_;
  std::map<int64_t, std::unique_ptr<FlutterPlatformViewLayer>> overlays_;
  // The GrContext that is currently used by all of the overlay surfaces.
  // We track this to know when the GrContext for the Flutter app has changed
  // so we can update the overlays with the new context.
  GrContext* overlays_gr_context_;
  SkISize frame_size_;

  // Method channel `OnDispose` calls adds the views to be disposed to this set to be disposed on
  // the next frame.
  std::unordered_set<int64_t> views_to_dispose_;

  // A vector of embedded view IDs according to their composition order.
  // The last ID in this vector belond to the that is composited on top of all others.
  std::vector<int64_t> composition_order_;

  // The latest composition order that was presented in Present().
  std::vector<int64_t> active_composition_order_;

  std::map<int64_t, std::unique_ptr<SkPictureRecorder>> picture_recorders_;

  void OnCreate(FlutterMethodCall* call, FlutterResult& result);
  void OnDispose(FlutterMethodCall* call, FlutterResult& result);
  void OnAcceptGesture(FlutterMethodCall* call, FlutterResult& result);
  void OnRejectGesture(FlutterMethodCall* call, FlutterResult& result);

  void DetachUnusedLayers();
  // Dispose the views in `views_to_dispose_`.
  void DisposeViews();
  void EnsureOverlayInitialized(int64_t overlay_id);
  void EnsureGLOverlayInitialized(int64_t overlay_id,
                                  std::shared_ptr<IOSGLContext> gl_context,
                                  GrContext* gr_context);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterPlatformViewsController);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_DARWIN_IOS_FRAMEWORK_SOURCE_FLUTTERPLATFORMVIEWS_INTERNAL_H_
