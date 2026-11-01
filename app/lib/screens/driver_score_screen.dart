// lib/screens/driver_score_screen.dart
// Phase 6E: Driver Score screen.
// Score is computed live from ViolationsProvider: no backend needed.
//
// ── FIELD NAME FIX GUIDE ────────────────────────────────────────────────────
// If you see "getter 'speedKmh' isn't defined for ViolationData", open
// lib/models/violation.dart and check the actual Dart field names for:
//   • recorded speed      → replace speedKmh    below
//   • speed limit         → replace speedLimitKmh below
//   • violation duration  → replace durationSec  below
// All three usages are in the _speed(), _limit(), _duration() helpers at the
// top of this file: fix them once there and nothing else needs changing.
// ────────────────────────────────────────────────────────────────────────────

import 'dart:math' as math;

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../models/violation.dart';
import '../providers/violations_provider.dart';
import '../theme/app_theme.dart';

// ── Field-name adapters (fix here if your model uses different names) ────────
double _speed(ViolationData v) => v.speed; // ← change if needed
double _limit(ViolationData v) => v.speedLimit.toDouble(); // ← change if needed
int _duration(ViolationData v) => v.durationSec; // ← change if needed

// ─────────────────────────────────────────────────────────────────────────────
// Score helpers
// ─────────────────────────────────────────────────────────────────────────────

double _computeScore(List<ViolationData> violations) {
  double score = 100.0;
  for (final v in violations) {
    final excess = _speed(v) - _limit(v);
    if (excess > 0) {
      final deduction = math.max(2.0, (excess * _duration(v)) / 10.0);
      score -= deduction;
    }
  }
  return score.clamp(0.0, 100.0);
}

Color _scoreColor(double score) {
  if (score >= 80) return AppColors.success;
  if (score >= 50) return AppColors.warning;
  return AppColors.danger;
}

String _scoreLabel(double score) {
  if (score >= 80) return 'Excellent';
  if (score >= 50) return 'Good';
  if (score >= 30) return 'Needs Improvement';
  return 'Poor';
}

// ─────────────────────────────────────────────────────────────────────────────
// 180° Arc painter
// ─────────────────────────────────────────────────────────────────────────────

class _ArcPainter extends CustomPainter {
  final double score;
  final Color arcColor;

  _ArcPainter({required this.score, required this.arcColor});

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height;
    final radius = size.width / 2 - 12;

    final trackPaint = Paint()
      ..color = AppColors.bgSurface
      ..strokeWidth = 18
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    final arcPaint = Paint()
      ..color = arcColor
      ..strokeWidth = 18
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    final rect = Rect.fromCircle(center: Offset(cx, cy), radius: radius);
    canvas.drawArc(rect, math.pi, math.pi, false, trackPaint);
    final sweepAngle = math.pi * (score / 100.0);
    canvas.drawArc(rect, math.pi, sweepAngle, false, arcPaint);
  }

  @override
  bool shouldRepaint(_ArcPainter old) =>
      old.score != score || old.arcColor != arcColor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main screen
// ─────────────────────────────────────────────────────────────────────────────

class DriverScoreScreen extends StatelessWidget {
  const DriverScoreScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<ViolationsProvider>(
      builder: (context, vp, _) {
        final violations = vp.violations;
        final score = _computeScore(violations);
        final color = _scoreColor(score);
        final label = _scoreLabel(score);

        return Scaffold(
          backgroundColor: AppColors.of(context).bgDark,
          appBar: AppBar(
            backgroundColor: AppColors.of(context).bgDark,
            elevation: 0,
            title: Text(
              'Driver Score',
              style: TextStyle(
                color: AppColors.of(context).textPrimary,
                fontSize: 20,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
          body: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
            child: violations.isEmpty
                ? _EmptyState(score: score, color: color)
                : Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      _ScoreArcSection(
                          score: score, color: color, label: label),
                      const SizedBox(height: 20),
                      _StatsRow(violations: violations),
                      const SizedBox(height: 20),
                      _TrendChart(violations: violations),
                      const SizedBox(height: 20),
                      _RecentBreakdown(violations: violations),
                    ],
                  ),
          ),
        );
      },
    );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Empty state
// ─────────────────────────────────────────────────────────────────────────────

class _EmptyState extends StatelessWidget {
  final double score;
  final Color color;
  const _EmptyState({required this.score, required this.color});

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        const SizedBox(height: 24),
        _ScoreArcSection(score: score, color: color, label: 'Excellent'),
        const SizedBox(height: 32),
        Container(
          padding: const EdgeInsets.all(24),
          decoration: BoxDecoration(
            color: AppColors.of(context).bgCard,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: AppColors.of(context).border),
          ),
          child: Row(
            children: [
              const Text('🏆', style: TextStyle(fontSize: 36)),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'No violations. Keep it up!',
                      style: TextStyle(
                        color: AppColors.of(context).textPrimary,
                        fontSize: 16,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      'You have a perfect score. Drive safely to maintain it.',
                      style: TextStyle(
                          color: AppColors.of(context).textSecondary,
                          fontSize: 13),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Arc section
// ─────────────────────────────────────────────────────────────────────────────

class _ScoreArcSection extends StatelessWidget {
  final double score;
  final Color color;
  final String label;
  const _ScoreArcSection(
      {required this.score, required this.color, required this.label});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        children: [
          SizedBox(
            width: 200,
            height: 110,
            child: Stack(
              alignment: Alignment.bottomCenter,
              children: [
                CustomPaint(
                  size: const Size(200, 110),
                  painter: _ArcPainter(score: score, arcColor: color),
                ),
                Padding(
                  padding: const EdgeInsets.only(bottom: 10),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        score.round().toString(),
                        style: TextStyle(
                          color: color,
                          fontSize: 56,
                          fontWeight: FontWeight.bold,
                          height: 1.0,
                        ),
                      ),
                      Text(
                        '/ 100',
                        style: TextStyle(
                            color: AppColors.of(context).textMuted,
                            fontSize: 13),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 8),
          Text(
            label.toUpperCase(),
            style: TextStyle(
              color: color,
              fontSize: 14,
              fontWeight: FontWeight.w700,
              letterSpacing: 1.5,
            ),
          ),
        ],
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Stats row
// ─────────────────────────────────────────────────────────────────────────────

class _StatsRow extends StatelessWidget {
  final List<ViolationData> violations;
  const _StatsRow({required this.violations});

  @override
  Widget build(BuildContext context) {
    final totalFines = violations.fold(0.0, (s, v) => s + v.fineAmount);

    double worstExcess = 0;
    for (final v in violations) {
      final excess = _speed(v) - _limit(v);
      if (excess > worstExcess) worstExcess = excess;
    }

    return Row(
      children: [
        _StatCard(
          label: 'Total Violations',
          value: violations.length.toString(),
          icon: Icons.warning_amber_rounded,
          iconColor: AppColors.warning,
        ),
        const SizedBox(width: 8),
        _StatCard(
          label: 'Total Fines',
          value: '${totalFines.round()} EGP',
          icon: Icons.receipt_long,
          iconColor: AppColors.danger,
        ),
        const SizedBox(width: 8),
        _StatCard(
          label: 'Worst Excess',
          value: worstExcess > 0 ? '+${worstExcess.round()} km/h' : '—',
          icon: Icons.speed,
          iconColor: AppColors.info,
        ),
      ],
    );
  }
}

class _StatCard extends StatelessWidget {
  final String label;
  final String value;
  final IconData icon;
  final Color iconColor;
  const _StatCard(
      {required this.label,
      required this.value,
      required this.icon,
      required this.iconColor});

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 14),
        decoration: BoxDecoration(
          color: AppColors.of(context).bgCard,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: AppColors.of(context).border),
        ),
        child: Column(
          children: [
            Icon(icon, color: iconColor, size: 20),
            const SizedBox(height: 6),
            Text(
              value,
              style: TextStyle(
                color: AppColors.of(context).textPrimary,
                fontSize: 14,
                fontWeight: FontWeight.w700,
              ),
              textAlign: TextAlign.center,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            const SizedBox(height: 2),
            Text(
              label,
              style: TextStyle(
                color: AppColors.of(context).textMuted,
                fontSize: 10,
              ),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// 14-day trend chart
// ─────────────────────────────────────────────────────────────────────────────

class _TrendChart extends StatelessWidget {
  final List<ViolationData> violations;
  const _TrendChart({required this.violations});

  List<FlSpot> _buildSpots() {
    final now = DateTime.now();
    double runningScore = 100.0;
    final spots = <FlSpot>[];

    for (int i = 13; i >= 0; i--) {
      final dayStart = DateTime(now.year, now.month, now.day - i);
      final dayEnd = dayStart.add(const Duration(days: 1));
      final dayViolations = violations.where((v) {
        final ts = v.timestamp;
        return ts.isAfter(dayStart) && ts.isBefore(dayEnd);
      }).toList();

      for (final v in dayViolations) {
        final excess = _speed(v) - _limit(v);
        if (excess > 0) {
          runningScore -= math.max(2.0, (excess * _duration(v)) / 10.0);
          runningScore = runningScore.clamp(0.0, 100.0);
        }
      }
      spots.add(FlSpot((13 - i).toDouble(), runningScore));
    }
    return spots;
  }

  @override
  Widget build(BuildContext context) {
    final spots = _buildSpots();
    final now = DateTime.now();

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgCard,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: AppColors.of(context).border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Score trend, last 14 days',
            style: TextStyle(
              color: AppColors.of(context).textPrimary,
              fontSize: 15,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 16),
          SizedBox(
            height: 160,
            child: LineChart(
              LineChartData(
                minX: 0,
                maxX: 13,
                minY: 0,
                maxY: 100,
                gridData: FlGridData(
                  show: true,
                  drawVerticalLine: false,
                  horizontalInterval: 25,
                  getDrawingHorizontalLine: (_) => FlLine(
                    color: AppColors.of(context).border,
                    strokeWidth: 1,
                  ),
                ),
                borderData: FlBorderData(show: false),
                titlesData: FlTitlesData(
                  leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      interval: 25,
                      reservedSize: 30,
                      getTitlesWidget: (v, _) => Text(
                        v.round().toString(),
                        style: TextStyle(
                            color: AppColors.of(context).textMuted,
                            fontSize: 10),
                      ),
                    ),
                  ),
                  rightTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  topTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      interval: 6,
                      reservedSize: 24,
                      getTitlesWidget: (value, _) {
                        final daysAgo = 13 - value.round();
                        final date =
                            DateTime(now.year, now.month, now.day - daysAgo);
                        return Padding(
                          padding: const EdgeInsets.only(top: 6),
                          child: Text(
                            DateFormat('d/M').format(date),
                            style: TextStyle(
                                color: AppColors.of(context).textMuted,
                                fontSize: 9),
                          ),
                        );
                      },
                    ),
                  ),
                ),
                lineBarsData: [
                  LineChartBarData(
                    spots: spots,
                    isCurved: true,
                    color: AppColors.of(context).primary,
                    barWidth: 2.5,
                    dotData: FlDotData(
                      show: true,
                      getDotPainter: (spot, _, __, ___) => FlDotCirclePainter(
                        radius: 3,
                        color: _scoreColor(spot.y),
                        strokeWidth: 0,
                      ),
                    ),
                    belowBarData: BarAreaData(
                      show: true,
                      color:
                          AppColors.of(context).primary.withValues(alpha: 0.08),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Recent violations breakdown
// ─────────────────────────────────────────────────────────────────────────────

class _RecentBreakdown extends StatelessWidget {
  final List<ViolationData> violations;
  const _RecentBreakdown({required this.violations});

  @override
  Widget build(BuildContext context) {
    final recent = violations.reversed.take(5).toList();

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgCard,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: AppColors.of(context).border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Recent Violations',
            style: TextStyle(
              color: AppColors.of(context).textPrimary,
              fontSize: 15,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 12),
          ...recent.map((v) {
            final excess = _speed(v) - _limit(v);
            final deduction = excess > 0
                ? math.max(2.0, (excess * _duration(v)) / 10.0)
                : 0.0;
            return Padding(
              padding: const EdgeInsets.symmetric(vertical: 8),
              child: Row(
                children: [
                  Container(
                    width: 36,
                    height: 36,
                    decoration: BoxDecoration(
                      color: AppColors.danger.withValues(alpha: 0.15),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: const Icon(Icons.speed,
                        color: AppColors.danger, size: 18),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '${_speed(v).round()} km/h in ${_limit(v).round()} zone',
                          style: TextStyle(
                            color: AppColors.of(context).textPrimary,
                            fontSize: 13,
                            fontWeight: FontWeight.w500,
                          ),
                        ),
                        const SizedBox(height: 2),
                        Text(
                          DateFormat('dd MMM, HH:mm').format(v.timestamp),
                          style: TextStyle(
                            color: AppColors.of(context).textMuted,
                            fontSize: 11,
                          ),
                        ),
                      ],
                    ),
                  ),
                  Text(
                    '−${deduction.round()} pts',
                    style: const TextStyle(
                      color: AppColors.danger,
                      fontSize: 13,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ],
              ),
            );
          }),
        ],
      ),
    );
  }
}
