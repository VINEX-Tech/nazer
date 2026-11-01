import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

class SpeedGauge extends StatefulWidget {
  final bool stale;
  final bool? overLimit;
  final double speed;
  final double speedLimit;
  final double maxSpeed;

  const SpeedGauge({
    super.key,
    required this.speed,
    required this.speedLimit,
    this.maxSpeed = 160,
    this.stale = false,
    this.overLimit,
  });

  @override
  State<SpeedGauge> createState() => _SpeedGaugeState();
}

class _SpeedGaugeState extends State<SpeedGauge>
    with SingleTickerProviderStateMixin {
  late AnimationController _controller;
  late Tween<double> _speedTween;
  late Animation<double> _speedAnim;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 300),
    );
    _speedTween = Tween<double>(begin: 0, end: widget.speed);
    _speedAnim = _speedTween.animate(
      CurvedAnimation(parent: _controller, curve: Curves.easeInOut),
    );
    _controller.forward();
  }

  @override
  void didUpdateWidget(SpeedGauge oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.speed != widget.speed) {
      _speedTween = Tween<double>(
        begin: _speedAnim.value, // animate from current position
        end: widget.speed,
      );
      _speedAnim = _speedTween.animate(
        CurvedAnimation(parent: _controller, curve: Curves.easeInOut),
      );
      _controller
        ..reset()
        ..forward();
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final isOverLimit =
        !widget.stale && (widget.overLimit ?? widget.speed > widget.speedLimit);

    return AnimatedBuilder(
      animation: _speedAnim,
      builder: (context, _) {
        final animatedSpeed = _speedAnim.value;
        final arcColor = isOverLimit
            ? AppColors.of(context).danger
            : AppColors.of(context).textPrimary;
        final textColor = isOverLimit
            ? AppColors.of(context).danger
            : AppColors.of(context).textPrimary;

        return SizedBox(
          width: 260,
          height: 260,
          child: Stack(
            alignment: Alignment.center,
            children: [
              // ── Arc painter ────────────────────────────────────────────────
              CustomPaint(
                size: const Size(260, 260),
                painter: _ArcPainter(
                  speed: animatedSpeed,
                  speedLimit: widget.speedLimit,
                  maxSpeed: widget.maxSpeed,
                  arcColor: arcColor,
                  gradient: !isOverLimit && !widget.stale,
                  trackColor: AppColors.of(context).bgSurface,
                  tickColor: AppColors.of(context).textPrimary,
                ),
              ),

              // ── Center content ─────────────────────────────────────────────
              Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  if (widget.stale)
                    Text('Last reported speed',
                        style: TextStyle(
                            color: AppColors.of(context).textSecondary,
                            fontSize: 12)),
                  // Speed number
                  Text(
                    animatedSpeed.toStringAsFixed(0),
                    style: TextStyle(
                      color: textColor,
                      fontSize: 64,
                      fontWeight: FontWeight.w700,
                      height: 1.0,
                    ),
                  ),
                  // "km/h" label
                  Text(
                    'km/h',
                    style: TextStyle(
                      color: AppColors.of(context).textSecondary,
                      fontSize: 14,
                    ),
                  ),
                  const SizedBox(height: 6),
                  if (isOverLimit)
                    Row(mainAxisSize: MainAxisSize.min, children: [
                      Icon(Icons.warning_rounded,
                          size: 18, color: AppColors.of(context).danger),
                      const SizedBox(width: 4),
                      Text('Over limit',
                          style: TextStyle(
                              color: AppColors.of(context).danger,
                              fontWeight: FontWeight.w700)),
                    ]),
                  // "Limit: XX" label
                  Text(
                    'Limit: ${widget.speedLimit.toStringAsFixed(0)}',
                    style: TextStyle(
                      color: AppColors.of(context).textSecondary,
                      fontSize: 12,
                    ),
                  ),
                ],
              ),
            ],
          ),
        );
      },
    );
  }
}

// ── CustomPainter ─────────────────────────────────────────────────────────────

class _ArcPainter extends CustomPainter {
  final double speed;
  final double speedLimit;
  final double maxSpeed;
  final Color arcColor;
  final bool gradient;
  final Color trackColor, tickColor;

  // Arc geometry constants
  // 220° sweep; start at 160° (measuring from positive x-axis = 0°, clockwise)
  static const double _startDeg = 160.0;
  static const double _sweepDeg = 220.0;
  static const double _strokeWidth = 14.0;

  _ArcPainter({
    required this.speed,
    required this.speedLimit,
    required this.maxSpeed,
    required this.arcColor,
    required this.gradient,
    required this.trackColor,
    required this.tickColor,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = (size.width - _strokeWidth) / 2;

    final rect = Rect.fromCircle(center: center, radius: radius);

    // ── Background arc ────────────────────────────────────────────────────────
    final bgPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = _strokeWidth
      ..strokeCap = StrokeCap.round
      ..color = trackColor;

    canvas.drawArc(
      rect,
      _toRadians(_startDeg),
      _toRadians(_sweepDeg),
      false,
      bgPaint,
    );

    // ── Speed fill arc ────────────────────────────────────────────────────────
    final speedFraction = (speed / maxSpeed).clamp(0.0, 1.0);
    final speedSweep = _sweepDeg * speedFraction;

    if (speedFraction > 0) {
      final fgPaint = Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = _strokeWidth
        ..strokeCap = StrokeCap.round
        ..color = arcColor;

      if (gradient) {
        fgPaint.shader = const SweepGradient(
          startAngle: 160 * math.pi / 180,
          endAngle: 380 * math.pi / 180,
          colors: [AppColors.primary, AppColors.accent],
        ).createShader(rect);
      }
      canvas.drawArc(
        rect,
        _toRadians(_startDeg),
        _toRadians(speedSweep),
        false,
        fgPaint,
      );
    }

    // ── Speed-limit tick ──────────────────────────────────────────────────────
    final limitFraction = (speedLimit / maxSpeed).clamp(0.0, 1.0);
    final limitAngleDeg = _startDeg + _sweepDeg * limitFraction;
    final limitAngleRad = _toRadians(limitAngleDeg);

    final tickOuter = center +
        Offset(math.cos(limitAngleRad), math.sin(limitAngleRad)) *
            (radius + _strokeWidth / 2 + 2);
    final tickInner = center +
        Offset(math.cos(limitAngleRad), math.sin(limitAngleRad)) *
            (radius - _strokeWidth / 2 - 2);

    final tickPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.5
      ..strokeCap = StrokeCap.round
      ..color = tickColor.withValues(alpha: 0.85);

    canvas.drawLine(tickInner, tickOuter, tickPaint);
  }

  @override
  bool shouldRepaint(_ArcPainter old) =>
      old.gradient != gradient ||
      old.maxSpeed != maxSpeed ||
      old.speed != speed ||
      old.speedLimit != speedLimit ||
      old.arcColor != arcColor ||
      old.trackColor != trackColor ||
      old.tickColor != tickColor;

  static double _toRadians(double degrees) => degrees * math.pi / 180.0;
}
