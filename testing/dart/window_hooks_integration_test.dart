// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// HACK: pretend to be dart.ui in order to access its internals
library dart.ui;

import 'dart:async';
// this needs to be imported because painting.dart expects it this way
import 'dart:collection' as collection;
import 'dart:convert';
import 'dart:developer' as developer;
import 'dart:math' as math;
import 'dart:nativewrappers'; // ignore: unused_import
import 'dart:typed_data';


import 'package:test/test.dart';

// HACK: these parts are to get access to private functions tested here.
part '../../lib/ui/compositing.dart';
part '../../lib/ui/geometry.dart';
part '../../lib/ui/hash_codes.dart';
part '../../lib/ui/hooks.dart';
part '../../lib/ui/lerp.dart';
part '../../lib/ui/natives.dart';
part '../../lib/ui/painting.dart';
part '../../lib/ui/pointer.dart';
part '../../lib/ui/semantics.dart';
part '../../lib/ui/text.dart';
part '../../lib/ui/window.dart';

void main() {
  group('window callback zones', () {
    VoidCallback originalOnMetricsChanged;
    VoidCallback originalOnLocaleChanged;
    FrameCallback originalOnBeginFrame;
    VoidCallback originalOnDrawFrame;
    TimingsCallback originalOnReportTimings;
    PointerDataPacketCallback originalOnPointerDataPacket;
    VoidCallback originalOnSemanticsEnabledChanged;
    SemanticsActionCallback originalOnSemanticsAction;
    PlatformMessageCallback originalOnPlatformMessage;
    VoidCallback originalOnTextScaleFactorChanged;

    setUp(() {
      originalOnMetricsChanged = window.onMetricsChanged;
      originalOnLocaleChanged = window.onLocaleChanged;
      originalOnBeginFrame = window.onBeginFrame;
      originalOnDrawFrame = window.onDrawFrame;
      originalOnReportTimings = window.onReportTimings;
      originalOnPointerDataPacket = window.onPointerDataPacket;
      originalOnSemanticsEnabledChanged = window.onSemanticsEnabledChanged;
      originalOnSemanticsAction = window.onSemanticsAction;
      originalOnPlatformMessage = window.onPlatformMessage;
      originalOnTextScaleFactorChanged = window.onTextScaleFactorChanged;
    });

    tearDown(() {
      window.onMetricsChanged = originalOnMetricsChanged;
      window.onLocaleChanged = originalOnLocaleChanged;
      window.onBeginFrame = originalOnBeginFrame;
      window.onDrawFrame = originalOnDrawFrame;
      window.onReportTimings = originalOnReportTimings;
      window.onPointerDataPacket = originalOnPointerDataPacket;
      window.onSemanticsEnabledChanged = originalOnSemanticsEnabledChanged;
      window.onSemanticsAction = originalOnSemanticsAction;
      window.onPlatformMessage = originalOnPlatformMessage;
      window.onTextScaleFactorChanged = originalOnTextScaleFactorChanged;
    });

    test('updateUserSettings can handle an empty object', () {
      // this should now throw.
      _updateUserSettingsData('{}');
      expect(true, equals(true));
    });

    test('onMetricsChanged preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      double devicePixelRatio;

      runZoned(() {
        innerZone = Zone.current;
        window.onMetricsChanged = () {
          runZone = Zone.current;
          devicePixelRatio = window.devicePixelRatio;
        };
      });

      window.onMetricsChanged();
      _updateWindowMetrics(0.1234, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(devicePixelRatio, equals(0.1234));
    });

    test('onLocaleChanged preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      Locale locale;

      runZoned(() {
        innerZone = Zone.current;
        window.onLocaleChanged = () {
          runZone = Zone.current;
          locale = window.locale;
        };
      });

      _updateLocales(<String>['en', 'US', '', '']);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(locale, equals(const Locale('en', 'US')));
    });

    test('onBeginFrame preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      Duration start;

      runZoned(() {
        innerZone = Zone.current;
        window.onBeginFrame = (Duration value) {
          runZone = Zone.current;
          start = value;
        };
      });

      _beginFrame(1234);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(start, equals(const Duration(microseconds: 1234)));
    });

    test('onDrawFrame preserves callback zone', () {
      Zone innerZone;
      Zone runZone;

      runZoned(() {
        innerZone = Zone.current;
        window.onDrawFrame = () {
          runZone = Zone.current;
        };
      });

      _drawFrame();
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
    });

    test('onReportTimings preserves callback zone', () {
      Zone innerZone;
      Zone runZone;

      runZoned(() {
        innerZone = Zone.current;
        window.onReportTimings = (List<FrameTiming> timings) {
          runZone = Zone.current;
        };
      });

      _reportTimings(<int>[]);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
    });

    test('onPointerDataPacket preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      PointerDataPacket data;

      runZoned(() {
        innerZone = Zone.current;
        window.onPointerDataPacket = (PointerDataPacket value) {
          runZone = Zone.current;
          data = value;
        };
      });

      final ByteData testData = ByteData.view(Uint8List(0).buffer);
      _dispatchPointerDataPacket(testData);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(data.data, equals(_unpackPointerDataPacket(testData).data));
    });

    test('onSemanticsEnabledChanged preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      bool enabled;

      runZoned(() {
        innerZone = Zone.current;
        window.onSemanticsEnabledChanged = () {
          runZone = Zone.current;
          enabled = window.semanticsEnabled;
        };
      });

      _updateSemanticsEnabled(window._semanticsEnabled);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(enabled, isNotNull);
      expect(enabled, equals(window._semanticsEnabled));
    });

    test('onSemanticsAction preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      int id;
      int action;

      runZoned(() {
        innerZone = Zone.current;
        window.onSemanticsAction = (int i, SemanticsAction a, ByteData _) {
          runZone = Zone.current;
          action = a.index;
          id = i;
        };
      });

      _dispatchSemanticsAction(1234, 4, null);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(id, equals(1234));
      expect(action, equals(4));
    });

    test('onPlatformMessage preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      String name;

      runZoned(() {
        innerZone = Zone.current;
        window.onPlatformMessage = (String value, _, __) {
          runZone = Zone.current;
          name = value;
        };
      });

      _dispatchPlatformMessage('testName', null, null);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(name, equals('testName'));
    });

    test('onTextScaleFactorChanged preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      double textScaleFactor;

      runZoned(() {
        innerZone = Zone.current;
        window.onTextScaleFactorChanged = () {
          runZone = Zone.current;
          textScaleFactor = window.textScaleFactor;
        };
      });

      window.onTextScaleFactorChanged();
      _updateTextScaleFactor(0.5);
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(textScaleFactor, equals(0.5));
    });

    test('onThemeBrightnessMode preserves callback zone', () {
      Zone innerZone;
      Zone runZone;
      Brightness platformBrightness;

      runZoned(() {
        innerZone = Zone.current;
        window.onPlatformBrightnessChanged = () {
          runZone = Zone.current;
          platformBrightness = window.platformBrightness;
        };
      });

      window.onPlatformBrightnessChanged();
      _updatePlatformBrightness('dark');
      expect(runZone, isNotNull);
      expect(runZone, same(innerZone));
      expect(platformBrightness, equals(Brightness.dark));
    });


    test('Window padding/insets/viewPadding', () {
      final double oldDPR = window.devicePixelRatio;
      final Size oldSize = window.physicalSize;
      final WindowPadding oldPadding = window.viewPadding;
      final WindowPadding oldInsets = window.viewInsets;

      _updateWindowMetrics(
        1.0,   // DPR
        800.0, // width
        600.0, // height
        50.0,  // padding top
        0.0,   // padding right
        40.0,  // padding bottom
        0.0,   // padding left
        0.0,   // inset top
        0.0,   // inset right
        0.0,   // inset bottom
        0.0,   // inset left
      );

      expect(window.viewInsets.bottom, 0.0);
      expect(window.viewPadding.bottom, 40.0);
      expect(window.padding.bottom, 40.0);

      _updateWindowMetrics(
        1.0,   // DPR
        800.0, // width
        600.0, // height
        50.0,  // padding top
        0.0,   // padding right
        40.0,  // padding bottom
        0.0,   // padding left
        0.0,   // inset top
        0.0,   // inset right
        400.0, // inset bottom
        0.0,   // inset left
      );

      expect(window.viewInsets.bottom, 400.0);
      expect(window.viewPadding.bottom, 40.0);
      expect(window.padding.bottom, 0.0);

       _updateWindowMetrics(
        oldDPR,             // DPR
        oldSize.width,      // width
        oldSize.height,     // height
        oldPadding.top,     // padding top
        oldPadding.right,   // padding right
        oldPadding.bottom,  // padding bottom
        oldPadding.left,    // padding left
        oldInsets.top,      // inset top
        oldInsets.right,    // inset right
        oldInsets.bottom,   // inset bottom
        oldInsets.left,     // inset left
      );
    });
  });
}
