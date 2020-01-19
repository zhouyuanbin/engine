// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class Target {
  const Target(this.stringValue, this.intValue);

  final String stringValue;
  final int intValue;

  void hit() {
    print('$stringValue $intValue');
  }
}
