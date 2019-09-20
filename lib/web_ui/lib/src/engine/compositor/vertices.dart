// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of engine;

Int32List _encodeColorList(List<ui.Color> colors) {
  final int colorCount = colors.length;
  final Int32List result = Int32List(colorCount);
  for (int i = 0; i < colorCount; ++i) result[i] = colors[i].value;
  return result;
}

Float32List _encodePointList(List<ui.Offset> points) {
  assert(points != null);
  final int pointCount = points.length;
  final Float32List result = Float32List(pointCount * 2);
  for (int i = 0; i < pointCount; ++i) {
    final int xIndex = i * 2;
    final int yIndex = xIndex + 1;
    final ui.Offset point = points[i];
    assert(_offsetIsValid(point));
    result[xIndex] = point.dx;
    result[yIndex] = point.dy;
  }
  return result;
}

class SkVertices implements ui.Vertices {
  js.JsObject skVertices;

  SkVertices(
    ui.VertexMode mode,
    List<ui.Offset> positions, {
    List<ui.Offset> textureCoordinates,
    List<ui.Color> colors,
    List<int> indices,
  })  : assert(mode != null),
        assert(positions != null) {
    if (textureCoordinates != null &&
        textureCoordinates.length != positions.length)
      throw ArgumentError(
          '"positions" and "textureCoordinates" lengths must match.');
    if (colors != null && colors.length != positions.length)
      throw ArgumentError('"positions" and "colors" lengths must match.');
    if (indices != null &&
        indices.any((int i) => i < 0 || i >= positions.length))
      throw ArgumentError(
          '"indices" values must be valid indices in the positions list.');

    final Float32List encodedPositions = _encodePointList(positions);
    final Float32List encodedTextureCoordinates = (textureCoordinates != null)
        ? _encodePointList(textureCoordinates)
        : null;
    final Int32List encodedColors =
        colors != null ? _encodeColorList(colors) : null;
    final Uint16List encodedIndices =
        indices != null ? Uint16List.fromList(indices) : null;

    if (!_init(mode, encodedPositions, encodedTextureCoordinates, encodedColors,
        encodedIndices))
      throw ArgumentError('Invalid configuration for vertices.');
  }

  SkVertices.raw(
    ui.VertexMode mode,
    Float32List positions, {
    Float32List textureCoordinates,
    Int32List colors,
    Uint16List indices,
  })  : assert(mode != null),
        assert(positions != null) {
    if (textureCoordinates != null &&
        textureCoordinates.length != positions.length)
      throw ArgumentError(
          '"positions" and "textureCoordinates" lengths must match.');
    if (colors != null && colors.length * 2 != positions.length)
      throw ArgumentError('"positions" and "colors" lengths must match.');
    if (indices != null &&
        indices.any((int i) => i < 0 || i >= positions.length))
      throw ArgumentError(
          '"indices" values must be valid indices in the positions list.');

    if (!_init(mode, positions, textureCoordinates, colors, indices))
      throw ArgumentError('Invalid configuration for vertices.');
  }

  bool _init(ui.VertexMode mode, Float32List positions,
      Float32List textureCoordinates, Int32List colors, Uint16List indices) {
    js.JsObject skVertexMode;
    switch (mode) {
      case ui.VertexMode.triangles:
        skVertexMode = canvasKit['VertexMode']['Triangles'];
        break;
      case ui.VertexMode.triangleStrip:
        skVertexMode = canvasKit['VertexMode']['TrianglesStrip'];
        break;
      case ui.VertexMode.triangleFan:
        skVertexMode = canvasKit['VertexMode']['TriangleFan'];
        break;
    }

    final js.JsObject vertices =
        canvasKit.callMethod('MakeSkVertices', <dynamic>[
      skVertexMode,
      _encodePoints(positions),
      _encodePoints(textureCoordinates),
      colors,
      null,
      null,
      indices,
    ]);

    if (vertices != null) {
      skVertices = vertices;
      return true;
    } else {
      return false;
    }
  }

  static _encodePoints(List<double> points) {
    if (points == null) return null;

    js.JsArray<js.JsArray<double>> encodedPoints =
        js.JsArray<js.JsArray<double>>();
    encodedPoints.length = points.length ~/ 2;
    for (int i = 0; i < points.length; i += 2) {
      encodedPoints[i ~/ 2] = makeSkPoint(ui.Offset(points[i], points[i + 1]));
    }
    return encodedPoints;
  }
}
