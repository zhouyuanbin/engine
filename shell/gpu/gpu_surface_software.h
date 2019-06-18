// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_GPU_GPU_SURFACE_SOFTWARE_H_
#define FLUTTER_SHELL_GPU_GPU_SURFACE_SOFTWARE_H_

#include "flutter/flow/embedded_views.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/memory/weak_ptr.h"
#include "flutter/shell/common/surface.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace flutter {

class GPUSurfaceSoftwareDelegate {
 public:
  virtual sk_sp<SkSurface> AcquireBackingStore(const SkISize& size) = 0;

  virtual bool PresentBackingStore(sk_sp<SkSurface> backing_store) = 0;

  virtual flutter::ExternalViewEmbedder* GetExternalViewEmbedder();
};

class GPUSurfaceSoftware : public Surface {
 public:
  GPUSurfaceSoftware(GPUSurfaceSoftwareDelegate* delegate);

  ~GPUSurfaceSoftware() override;

  // |Surface|
  bool IsValid() override;

  // |Surface|
  std::unique_ptr<SurfaceFrame> AcquireFrame(const SkISize& size) override;

  // |Surface|
  SkMatrix GetRootTransformation() const override;

  // |Surface|
  GrContext* GetContext() override;

  // |Surface|
  flutter::ExternalViewEmbedder* GetExternalViewEmbedder() override;

 private:
  GPUSurfaceSoftwareDelegate* delegate_;
  fml::WeakPtrFactory<GPUSurfaceSoftware> weak_factory_;

  FML_DISALLOW_COPY_AND_ASSIGN(GPUSurfaceSoftware);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_GPU_GPU_SURFACE_SOFTWARE_H_
