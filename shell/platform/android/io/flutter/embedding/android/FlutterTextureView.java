// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.embedding.android;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Surface;
import android.view.TextureView;

import java.util.HashSet;
import java.util.Set;

import io.flutter.embedding.engine.renderer.FlutterRenderer;
import io.flutter.embedding.engine.renderer.OnFirstFrameRenderedListener;

/**
 * Paints a Flutter UI on a {@link SurfaceTexture}.
 *
 * To begin rendering a Flutter UI, the owner of this {@code FlutterTextureView} must invoke
 * {@link #attachToRenderer(FlutterRenderer)} with the desired {@link FlutterRenderer}.
 *
 * To stop rendering a Flutter UI, the owner of this {@code FlutterTextureView} must invoke
 * {@link #detachFromRenderer()}.
 *
 * A {@code FlutterTextureView} is intended for situations where a developer needs to render
 * a Flutter UI, but does not require any keyboard input, gesture input, accessibility
 * integrations or any other interactivity beyond rendering. If standard interactivity is
 * desired, consider using a {@link FlutterView} which provides all of these behaviors and
 * utilizes a {@code FlutterTextureView} internally.
 */
public class FlutterTextureView extends TextureView implements FlutterRenderer.RenderSurface {
  private static final String TAG = "FlutterTextureView";

  private boolean isSurfaceAvailableForRendering = false;
  private boolean isAttachedToFlutterRenderer = false;
  @Nullable
  private FlutterRenderer flutterRenderer;
  @NonNull
  private Set<OnFirstFrameRenderedListener> onFirstFrameRenderedListeners = new HashSet<>();

  // Connects the {@code SurfaceTexture} beneath this {@code TextureView} with Flutter's native code.
  // Callbacks are received by this Object and then those messages are forwarded to our
  // FlutterRenderer, and then on to the JNI bridge over to native Flutter code.
  private final SurfaceTextureListener surfaceTextureListener = new SurfaceTextureListener() {
    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture surfaceTexture, int width, int height) {
      Log.d(TAG, "SurfaceTextureListener.onSurfaceTextureAvailable()");
      isSurfaceAvailableForRendering = true;

      // If we're already attached to a FlutterRenderer then we're now attached to both a renderer
      // and the Android window, so we can begin rendering now.
      if (isAttachedToFlutterRenderer) {
        Log.d(TAG, "Already attached to renderer. Notifying of surface creation.");
        connectSurfaceToRenderer();
      }
    }

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
      if (isAttachedToFlutterRenderer) {
        changeSurfaceSize(width, height);
      }
    }

    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture surface) {
      // Invoked every time a new frame is available. We don't care.
    }

    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
      Log.d(TAG, "SurfaceTextureListener.onSurfaceTextureDestroyed()");
      isSurfaceAvailableForRendering = false;

      // If we're attached to a FlutterRenderer then we need to notify it that our SurfaceTexture
      // has been destroyed.
      if (isAttachedToFlutterRenderer) {
        disconnectSurfaceFromRenderer();
      }

      // Return true to indicate that no further painting will take place
      // within this SurfaceTexture.
      return true;
    }
  };

  /**
   * Constructs a {@code FlutterTextureView} programmatically, without any XML attributes.
   */
  public FlutterTextureView(Context context) {
    this(context, null);
  }

  /**
   * Constructs a {@code FlutterTextureView} in an XML-inflation-compliant manner.
   */
  public FlutterTextureView(Context context, AttributeSet attrs) {
    super(context, attrs);
    init();
  }

  private void init() {
    // Listen for when our underlying SurfaceTexture becomes available, changes size, or
    // gets destroyed, and take the appropriate actions.
    setSurfaceTextureListener(surfaceTextureListener);
  }

  /**
   * Invoked by the owner of this {@code FlutterTextureView} when it wants to begin rendering
   * a Flutter UI to this {@code FlutterTextureView}.
   *
   * If an Android {@link SurfaceTexture} is available, this method will give that
   * {@link SurfaceTexture} to the given {@link FlutterRenderer} to begin rendering
   * Flutter's UI to this {@code FlutterTextureView}.
   *
   * If no Android {@link SurfaceTexture} is available yet, this {@code FlutterTextureView}
   * will wait until a {@link SurfaceTexture} becomes available and then give that
   * {@link SurfaceTexture} to the given {@link FlutterRenderer} to begin rendering
   * Flutter's UI to this {@code FlutterTextureView}.
   */
  public void attachToRenderer(@NonNull FlutterRenderer flutterRenderer) {
    if (this.flutterRenderer != null) {
      this.flutterRenderer.detachFromRenderSurface();
    }

    this.flutterRenderer = flutterRenderer;
    isAttachedToFlutterRenderer = true;

    // If we're already attached to an Android window then we're now attached to both a renderer
    // and the Android window. We can begin rendering now.
    if (isSurfaceAvailableForRendering) {
      connectSurfaceToRenderer();
    }
  }

  /**
   * Invoked by the owner of this {@code FlutterTextureView} when it no longer wants to render
   * a Flutter UI to this {@code FlutterTextureView}.
   *
   * This method will cease any on-going rendering from Flutter to this {@code FlutterTextureView}.
   */
  public void detachFromRenderer() {
    if (flutterRenderer != null) {
      // If we're attached to an Android window then we were rendering a Flutter UI. Now that
      // this FlutterTextureView is detached from the FlutterRenderer, we need to stop rendering.
      if (getWindowToken() != null) {
        disconnectSurfaceFromRenderer();
      }

      flutterRenderer = null;
      isAttachedToFlutterRenderer = false;
    } else {
      Log.w(TAG, "detachFromRenderer() invoked when no FlutterRenderer was attached.");
    }
  }

  // FlutterRenderer and getSurfaceTexture() must both be non-null.
  private void connectSurfaceToRenderer() {
    if (flutterRenderer == null || getSurfaceTexture() == null) {
      throw new IllegalStateException("connectSurfaceToRenderer() should only be called when flutterRenderer and getSurfaceTexture() are non-null.");
    }

    flutterRenderer.surfaceCreated(new Surface(getSurfaceTexture()));
  }

  // FlutterRenderer must be non-null.
  private void changeSurfaceSize(int width, int height) {
    if (flutterRenderer == null) {
      throw new IllegalStateException("changeSurfaceSize() should only be called when flutterRenderer is non-null.");
    }

    flutterRenderer.surfaceChanged(width, height);
  }

  // FlutterRenderer must be non-null.
  private void disconnectSurfaceFromRenderer() {
    if (flutterRenderer == null) {
      throw new IllegalStateException("disconnectSurfaceFromRenderer() should only be called when flutterRenderer is non-null.");
    }

    flutterRenderer.surfaceDestroyed();
  }

  /**
   * Adds the given {@code listener} to this {@code FlutterTextureView}, to be notified upon Flutter's
   * first rendered frame.
   */
  @Override
  public void addOnFirstFrameRenderedListener(@NonNull OnFirstFrameRenderedListener listener) {
    onFirstFrameRenderedListeners.add(listener);
  }

  /**
   * Removes the given {@code listener}, which was previously added with
   * {@link #addOnFirstFrameRenderedListener(OnFirstFrameRenderedListener)}.
   */
  @Override
  public void removeOnFirstFrameRenderedListener(@NonNull OnFirstFrameRenderedListener listener) {
    onFirstFrameRenderedListeners.remove(listener);
  }

  @Override
  public void onFirstFrameRendered() {
    // TODO(mattcarroll): decide where this method should live and what it needs to do.
    Log.d(TAG, "onFirstFrameRendered()");

    for (OnFirstFrameRenderedListener listener : onFirstFrameRenderedListeners) {
      listener.onFirstFrameRendered();
    }
  }
}
