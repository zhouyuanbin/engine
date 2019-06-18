// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.view;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.util.Log;
import io.flutter.BuildConfig;
import io.flutter.util.PathUtils;

import java.io.File;
import java.io.IOException;
import java.util.*;

/**
 * A class to intialize the Flutter engine.
 */
public class FlutterMain {
    private static final String TAG = "FlutterMain";

    // Must match values in sky::switches
    private static final String AOT_SHARED_LIBRARY_NAME = "aot-shared-library-name";
    private static final String SNAPSHOT_ASSET_PATH_KEY = "snapshot-asset-path";
    private static final String VM_SNAPSHOT_DATA_KEY = "vm-snapshot-data";
    private static final String ISOLATE_SNAPSHOT_DATA_KEY = "isolate-snapshot-data";
    private static final String FLUTTER_ASSETS_DIR_KEY = "flutter-assets-dir";

    // XML Attribute keys supported in AndroidManifest.xml
    public static final String PUBLIC_AOT_SHARED_LIBRARY_NAME =
        FlutterMain.class.getName() + '.' + AOT_SHARED_LIBRARY_NAME;
    public static final String PUBLIC_VM_SNAPSHOT_DATA_KEY =
        FlutterMain.class.getName() + '.' + VM_SNAPSHOT_DATA_KEY;
    public static final String PUBLIC_ISOLATE_SNAPSHOT_DATA_KEY =
        FlutterMain.class.getName() + '.' + ISOLATE_SNAPSHOT_DATA_KEY;
    public static final String PUBLIC_FLUTTER_ASSETS_DIR_KEY =
        FlutterMain.class.getName() + '.' + FLUTTER_ASSETS_DIR_KEY;

    // Resource names used for components of the precompiled snapshot.
    private static final String DEFAULT_AOT_SHARED_LIBRARY_NAME = "libapp.so";
    private static final String DEFAULT_VM_SNAPSHOT_DATA = "vm_snapshot_data";
    private static final String DEFAULT_ISOLATE_SNAPSHOT_DATA = "isolate_snapshot_data";
    private static final String DEFAULT_LIBRARY = "libflutter.so";
    private static final String DEFAULT_KERNEL_BLOB = "kernel_blob.bin";
    private static final String DEFAULT_FLUTTER_ASSETS_DIR = "flutter_assets";

    @NonNull
    private static String fromFlutterAssets(@NonNull String filePath) {
        return sFlutterAssetsDir + File.separator + filePath;
    }

    // Mutable because default values can be overridden via config properties
    private static String sAotSharedLibraryName = DEFAULT_AOT_SHARED_LIBRARY_NAME;
    private static String sVmSnapshotData = DEFAULT_VM_SNAPSHOT_DATA;
    private static String sIsolateSnapshotData = DEFAULT_ISOLATE_SNAPSHOT_DATA;
    private static String sFlutterAssetsDir = DEFAULT_FLUTTER_ASSETS_DIR;

    private static boolean sInitialized = false;

    @Nullable
    private static ResourceExtractor sResourceExtractor;
    @Nullable
    private static Settings sSettings;

    public static class Settings {
        private String logTag;

        @Nullable
        public String getLogTag() {
            return logTag;
        }

        /**
         * Set the tag associated with Flutter app log messages.
         * @param tag Log tag.
         */
        public void setLogTag(String tag) {
            logTag = tag;
        }
    }

    /**
     * Starts initialization of the native system.
     * @param applicationContext The Android application context.
     */
    public static void startInitialization(@NonNull Context applicationContext) {
        startInitialization(applicationContext, new Settings());
    }

    /**
     * Starts initialization of the native system.
     * @param applicationContext The Android application context.
     * @param settings Configuration settings.
     */
    public static void startInitialization(@NonNull Context applicationContext, @NonNull Settings settings) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
          throw new IllegalStateException("startInitialization must be called on the main thread");
        }
        // Do not run startInitialization more than once.
        if (sSettings != null) {
          return;
        }

        sSettings = settings;

        long initStartTimestampMillis = SystemClock.uptimeMillis();
        initConfig(applicationContext);
        initResources(applicationContext);

        System.loadLibrary("flutter");

        // We record the initialization time using SystemClock because at the start of the
        // initialization we have not yet loaded the native library to call into dart_tools_api.h.
        // To get Timeline timestamp of the start of initialization we simply subtract the delta
        // from the Timeline timestamp at the current moment (the assumption is that the overhead
        // of the JNI call is negligible).
        long initTimeMillis = SystemClock.uptimeMillis() - initStartTimestampMillis;
        nativeRecordStartTimestamp(initTimeMillis);
    }

    /**
     * Blocks until initialization of the native system has completed.
     * @param applicationContext The Android application context.
     * @param args Flags sent to the Flutter runtime.
     */
    public static void ensureInitializationComplete(@NonNull Context applicationContext, @Nullable String[] args) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
          throw new IllegalStateException("ensureInitializationComplete must be called on the main thread");
        }
        if (sSettings == null) {
          throw new IllegalStateException("ensureInitializationComplete must be called after startInitialization");
        }
        if (sInitialized) {
            return;
        }
        try {
            if (sResourceExtractor != null) {
                sResourceExtractor.waitForCompletion();
            }

            List<String> shellArgs = new ArrayList<>();
            shellArgs.add("--icu-symbol-prefix=_binary_icudtl_dat");

            ApplicationInfo applicationInfo = getApplicationInfo(applicationContext);
            shellArgs.add("--icu-native-lib-path=" + applicationInfo.nativeLibraryDir + File.separator + DEFAULT_LIBRARY);

            if (args != null) {
                Collections.addAll(shellArgs, args);
            }
            if (BuildConfig.DEBUG) {
                shellArgs.add("--" + SNAPSHOT_ASSET_PATH_KEY + "=" + PathUtils.getDataDirectory(applicationContext) + "/" + sFlutterAssetsDir);
                shellArgs.add("--" + VM_SNAPSHOT_DATA_KEY + "=" + sVmSnapshotData);
                shellArgs.add("--" + ISOLATE_SNAPSHOT_DATA_KEY + "=" + sIsolateSnapshotData);
            } else {
                shellArgs.add("--" + AOT_SHARED_LIBRARY_NAME + "=" + sAotSharedLibraryName);
            }

            shellArgs.add("--cache-dir-path=" + PathUtils.getCacheDirectory(applicationContext));
            if (sSettings.getLogTag() != null) {
                shellArgs.add("--log-tag=" + sSettings.getLogTag());
            }

            String appBundlePath = findAppBundlePath(applicationContext);
            String appStoragePath = PathUtils.getFilesDir(applicationContext);
            String engineCachesPath = PathUtils.getCacheDirectory(applicationContext);
            nativeInit(applicationContext, shellArgs.toArray(new String[0]),
                appBundlePath, appStoragePath, engineCachesPath);

            sInitialized = true;
        } catch (Exception e) {
            Log.e(TAG, "Flutter initialization failed.", e);
            throw new RuntimeException(e);
        }
    }

    /**
     * Same as {@link #ensureInitializationComplete(Context, String[])} but waiting on a background
     * thread, then invoking {@code callback} on the {@code callbackHandler}.
     */
    public static void ensureInitializationCompleteAsync(
        @NonNull Context applicationContext,
        @Nullable String[] args,
        @NonNull Handler callbackHandler,
        @NonNull Runnable callback
    ) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("ensureInitializationComplete must be called on the main thread");
        }
        if (sSettings == null) {
            throw new IllegalStateException("ensureInitializationComplete must be called after startInitialization");
        }
        if (sInitialized) {
            return;
        }
        new Thread(new Runnable() {
            @Override
            public void run() {
                if (sResourceExtractor != null) {
                    sResourceExtractor.waitForCompletion();
                }
                new Handler(Looper.getMainLooper()).post(new Runnable() {
                    @Override
                    public void run() {
                        ensureInitializationComplete(applicationContext.getApplicationContext(), args);
                        callbackHandler.post(callback);
                    }
                });
            }
        }).start();
    }

    private static native void nativeInit(Context context, String[] args, String bundlePath, String appStoragePath, String engineCachesPath);
    private static native void nativeRecordStartTimestamp(long initTimeMillis);

    @NonNull
    private static ApplicationInfo getApplicationInfo(@NonNull Context applicationContext) {
        try {
            return applicationContext
                .getPackageManager()
                .getApplicationInfo(applicationContext.getPackageName(), PackageManager.GET_META_DATA);
        } catch (PackageManager.NameNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    /**
     * Initialize our Flutter config values by obtaining them from the
     * manifest XML file, falling back to default values.
     */
    private static void initConfig(@NonNull Context applicationContext) {
        Bundle metadata = getApplicationInfo(applicationContext).metaData;

        // There isn't a `<meta-data>` tag as a direct child of `<application>` in
        // `AndroidManifest.xml`.
        if (metadata == null) {
            return;
        }

        sAotSharedLibraryName = metadata.getString(PUBLIC_AOT_SHARED_LIBRARY_NAME, DEFAULT_AOT_SHARED_LIBRARY_NAME);
        sFlutterAssetsDir = metadata.getString(PUBLIC_FLUTTER_ASSETS_DIR_KEY, DEFAULT_FLUTTER_ASSETS_DIR);

        sVmSnapshotData = metadata.getString(PUBLIC_VM_SNAPSHOT_DATA_KEY, DEFAULT_VM_SNAPSHOT_DATA);
        sIsolateSnapshotData = metadata.getString(PUBLIC_ISOLATE_SNAPSHOT_DATA_KEY, DEFAULT_ISOLATE_SNAPSHOT_DATA);
    }

    /**
     * Extract assets out of the APK that need to be cached as uncompressed
     * files on disk.
     */
    private static void initResources(@NonNull Context applicationContext) {
        new ResourceCleaner(applicationContext).start();

        final String dataDirPath = PathUtils.getDataDirectory(applicationContext);

        if (BuildConfig.DEBUG) {
            final String packageName = applicationContext.getPackageName();
            final PackageManager packageManager = applicationContext.getPackageManager();
            final AssetManager assetManager = applicationContext.getResources().getAssets();
            sResourceExtractor = new ResourceExtractor(dataDirPath, packageName, packageManager, assetManager);

            // In debug/JIT mode these assets will be written to disk and then
            // mapped into memory so they can be provided to the Dart VM.
            sResourceExtractor
                .addResource(fromFlutterAssets(sVmSnapshotData))
                .addResource(fromFlutterAssets(sIsolateSnapshotData))
                .addResource(fromFlutterAssets(DEFAULT_KERNEL_BLOB));

            sResourceExtractor.start();
        } else {
            // AOT modes obtain compiled Dart assets from a ELF library that does
            // not need to be extracted out of the APK.
            // Create an empty directory that can be passed as the bundle path
            // in the engine RunBundle API.
            new File(dataDirPath, sFlutterAssetsDir).mkdirs();
        }
    }

    @Nullable
    public static String findAppBundlePath(@NonNull Context applicationContext) {
        String dataDirectory = PathUtils.getDataDirectory(applicationContext);
        File appBundle = new File(dataDirectory, sFlutterAssetsDir);
        return appBundle.exists() ? appBundle.getPath() : null;
    }

    /**
     * Returns the file name for the given asset.
     * The returned file name can be used to access the asset in the APK
     * through the {@link android.content.res.AssetManager} API.
     *
     * @param asset the name of the asset. The name can be hierarchical
     * @return      the filename to be used with {@link android.content.res.AssetManager}
     */
    @NonNull
    public static String getLookupKeyForAsset(@NonNull String asset) {
        return fromFlutterAssets(asset);
    }

    /**
     * Returns the file name for the given asset which originates from the
     * specified packageName. The returned file name can be used to access
     * the asset in the APK through the {@link android.content.res.AssetManager} API.
     *
     * @param asset       the name of the asset. The name can be hierarchical
     * @param packageName the name of the package from which the asset originates
     * @return            the file name to be used with {@link android.content.res.AssetManager}
     */
    @NonNull
    public static String getLookupKeyForAsset(@NonNull String asset, @NonNull String packageName) {
        return getLookupKeyForAsset(
            "packages" + File.separator + packageName + File.separator + asset);
    }
}
