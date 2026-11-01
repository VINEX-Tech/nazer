import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:latlong2/latlong.dart';
import 'package:nazer_app/models/tracking.dart';
import 'package:nazer_app/theme/app_theme.dart';
import 'package:nazer_app/widgets/vehicle_map.dart';
import 'package:nazer_app/widgets/speed_gauge.dart';

double contrast(Color a, Color b) {
  final x = a.computeLuminance(), y = b.computeLuminance();
  return ((x > y ? x : y) + .05) / ((x > y ? y : x) + .05);
}

void main() {
  test('gradient action labels use the large-text AA threshold', () {
    for (final theme in [AppTheme.dark(), AppTheme.light()]) {
      final style = theme.elevatedButtonTheme.style!.textStyle!.resolve({})!;
      expect(style.fontSize, greaterThanOrEqualTo(18.67));
      expect(style.fontWeight, FontWeight.w700);
    }
  });
  for (final dark in [true, false]) {
    final palette = AppPalette(dark);
    test('AA text contrast for ${dark ? 'dark' : 'light'} surfaces', () {
      for (final bg in [palette.bgDark, palette.bgCard, palette.bgSurface]) {
        for (final fg in [
          palette.textPrimary,
          palette.textSecondary,
          palette.textMuted,
          palette.primary,
          palette.success,
          palette.warning,
          palette.danger
        ]) {
          expect(contrast(fg, bg), greaterThanOrEqualTo(4.5));
        }
      }
      for (final fg in [palette.success, palette.warning, palette.danger]) {
        final tinted =
            Color.alphaBlend(fg.withValues(alpha: .15), palette.bgCard);
        expect(contrast(fg, tinted), greaterThanOrEqualTo(4.5));
      }
      for (final bg in [AppColors.primary, AppColors.accent]) {
        expect(contrast(AppColors.bgDark, bg), greaterThanOrEqualTo(3));
      }
    });
    testWidgets(
        'trail keeps brand colour across speeds in ${dark ? 'dark' : 'light'}',
        (tester) async {
      final points = [0.0, 79.0, 120.0]
          .indexed
          .map((entry) => TrackPoint(
              timestamp: DateTime(2026, 10, 16, 12, entry.$1),
              latitude: 30 + entry.$1 / 100,
              longitude: 31,
              speed: entry.$2))
          .toList();
      await tester.pumpWidget(MaterialApp(
          theme: dark ? AppTheme.dark() : AppTheme.light(),
          home: Scaffold(
              body: FlutterMap(
                  options: const MapOptions(initialCenter: LatLng(30, 31)),
                  children: [VehicleTrail(points: points)]))));
      final line = tester
          .widget<PolylineLayer>(find.byType(PolylineLayer))
          .polylines
          .single;
      expect(line.color, palette.primary);
      expect(line.points.length, 3);
      expect(line.points.first, const LatLng(30, 31));
      expect(line.points.last, const LatLng(30.02, 31));
    });
  }
  testWidgets('stale gauge does not present an active overspeed alert',
      (tester) async {
    await tester.pumpWidget(MaterialApp(
        theme: AppTheme.light(),
        home: const Scaffold(
            body: SpeedGauge(speed: 120, speedLimit: 80, stale: true))));
    await tester.pump(const Duration(seconds: 1));
    expect(find.text('Last reported speed'), findsOneWidget);
    expect(find.text('Over limit'), findsNothing);
  });
}
