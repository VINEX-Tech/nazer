// lib/screens/violation_detail_screen.dart
// Phase 6D: Full implementation matching ViolationDetailScreen.tsx design.
//
// Fix: replaced '../theme/app_colors.dart' with '../theme/app_theme.dart'
//      (AppColors is defined in app_theme.dart, no separate app_colors.dart exists).

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:go_router/go_router.dart';
import 'package:latlong2/latlong.dart';
import 'package:provider/provider.dart';
import 'package:intl/intl.dart';

import '../models/violation.dart';
import '../providers/violations_provider.dart';
import '../theme/app_theme.dart';
import '../services/map_tile_provider.dart';

class ViolationDetailScreen extends StatelessWidget {
  final String violationId;
  const ViolationDetailScreen({super.key, required this.violationId});

  @override
  Widget build(BuildContext context) {
    final provider = context.watch<ViolationsProvider>();
    final ViolationData? violation = provider.violations
        .where((v) => v.violationId == violationId)
        .firstOrNull;

    if (violation == null) {
      return Scaffold(
        backgroundColor: AppColors.of(context).bgDark,
        appBar: AppBar(
          backgroundColor: AppColors.of(context).bgDark,
          leading: const BackButton(color: AppColors.white),
          title: const Text('Violation'),
        ),
        body: Center(
          child: Text(
            'Violation not found',
            style: TextStyle(color: AppColors.of(context).textSecondary),
          ),
        ),
      );
    }

    return Scaffold(
      backgroundColor: AppColors.of(context).bgDark,
      body: SafeArea(
        child: Column(
          children: [
            // ── Header ──────────────────────────────────────────────────────
            _Header(violationId: violationId),

            // ── Scrollable body ──────────────────────────────────────────────
            Expanded(
              child: SingleChildScrollView(
                child: Column(
                  children: [
                    // Map
                    _MapView(violation: violation),

                    // Detail card (overlaps map slightly)
                    Transform.translate(
                      offset: const Offset(0, -24),
                      child: Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 16),
                        child: _DetailCard(violation: violation),
                      ),
                    ),

                    // Action area
                    Padding(
                      padding: const EdgeInsets.fromLTRB(16, 0, 16, 32),
                      child: _ActionArea(violation: violation),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Header ────────────────────────────────────────────────────────────────────

class _Header extends StatelessWidget {
  final String violationId;
  const _Header({required this.violationId});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(4, 8, 16, 8),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgDark,
        border: Border(
          bottom: BorderSide(color: AppColors.of(context).border),
        ),
      ),
      child: Row(
        children: [
          IconButton(
            icon: Icon(Icons.arrow_back_ios_new_rounded,
                color: AppColors.of(context).textPrimary, size: 20),
            onPressed: () => context.go('/violations'),
          ),
          Expanded(
            child: Text(
              'Violation #${_shortId(violationId)}',
              style: TextStyle(
                color: AppColors.of(context).textPrimary,
                fontSize: 18,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
        ],
      ),
    );
  }

  String _shortId(String id) {
    final parts = id.split('_');
    return parts.length >= 3 ? parts.last : id;
  }
}

// ── Map view ──────────────────────────────────────────────────────────────────

class _MapView extends StatelessWidget {
  final ViolationData violation;
  const _MapView({required this.violation});

  @override
  Widget build(BuildContext context) {
    final center = LatLng(violation.latitude, violation.longitude);
    final hasCoords = violation.latitude != 0.0 || violation.longitude != 0.0;

    if (!hasCoords) {
      return Container(
        height: 210,
        color: AppColors.of(context).bgCard,
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.location_off_rounded,
                  color: AppColors.of(context).textMuted, size: 36),
              const SizedBox(height: 8),
              Text(
                'Location unavailable',
                style: TextStyle(
                    color: AppColors.of(context).textMuted, fontSize: 13),
              ),
            ],
          ),
        ),
      );
    }

    return SizedBox(
      height: 210,
      child: FlutterMap(
        options: MapOptions(
          initialCenter: center,
          initialZoom: 15,
          interactionOptions: const InteractionOptions(
            flags: InteractiveFlag.none,
          ),
        ),
        children: [
          TileLayer(
            urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
            userAgentPackageName: 'com.nazer.app',
            tileProvider: MapTileProvider(),
            panBuffer: 0,
          ),
          const Align(
              alignment: Alignment.bottomCenter,
              child: ColoredBox(
                  color: AppColors.bgCard,
                  child: Text('© OpenStreetMap contributors',
                      style: TextStyle(color: AppColors.white, fontSize: 11)))),
          MarkerLayer(
            markers: [
              Marker(
                point: center,
                width: 48,
                height: 48,
                child: const _ViolationMarker(),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _ViolationMarker extends StatefulWidget {
  const _ViolationMarker();

  @override
  State<_ViolationMarker> createState() => _ViolationMarkerState();
}

class _ViolationMarkerState extends State<_ViolationMarker>
    with SingleTickerProviderStateMixin {
  late final AnimationController _ctrl;
  late final Animation<double> _scale;

  @override
  void initState() {
    super.initState();
    _ctrl = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    )..repeat(reverse: true);
    _scale = Tween<double>(begin: 1.0, end: 1.12).animate(
      CurvedAnimation(parent: _ctrl, curve: Curves.easeInOut),
    );
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _scale,
      builder: (_, __) => Transform.scale(
        scale: _scale.value,
        child: Container(
          width: 48,
          height: 48,
          decoration: BoxDecoration(
            color: AppColors.danger,
            shape: BoxShape.circle,
            boxShadow: [
              BoxShadow(
                color: AppColors.danger.withValues(alpha: 0.45),
                blurRadius: 10,
                spreadRadius: 2,
              ),
            ],
          ),
          child: const Icon(Icons.warning_rounded,
              color: AppColors.white, size: 26),
        ),
      ),
    );
  }
}

// ── Detail card ───────────────────────────────────────────────────────────────

class _DetailCard extends StatelessWidget {
  final ViolationData violation;
  const _DetailCard({required this.violation});

  @override
  Widget build(BuildContext context) {
    final excess = violation.excessSpeed;
    final dateStr = DateFormat('MMM d, yyyy').format(violation.timestamp);
    final timeStr = DateFormat('h:mm a').format(violation.timestamp);

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgCard,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [
          BoxShadow(
            color: AppColors.black.withValues(alpha: 0.3),
            blurRadius: 12,
            offset: const Offset(0, 4),
          ),
        ],
        border: Border.all(color: AppColors.of(context).border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Title row
          Row(
            children: [
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  color: AppColors.danger.withValues(alpha: 0.15),
                  shape: BoxShape.circle,
                ),
                child: const Icon(Icons.warning_rounded,
                    color: AppColors.danger, size: 26),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Speed Violation',
                      style: TextStyle(
                        color: AppColors.of(context).textPrimary,
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 4),
                    _StatusBadge(isPaid: violation.isPaid),
                  ],
                ),
              ),
            ],
          ),

          const SizedBox(height: 20),

          // Data rows
          _DataRow(
              label: 'Your Speed',
              value: '${violation.speed.toStringAsFixed(0)} km/h',
              valueColor: AppColors.danger),
          _DataRow(label: 'Speed Limit', value: '${violation.speedLimit} km/h'),
          _DataRow(
              label: 'Exceeded by',
              value: '+${excess.toStringAsFixed(0)} km/h',
              valueColor: AppColors.warning),
          if (violation.durationSec > 0)
            _DataRow(
                label: 'Duration',
                value: '${violation.durationSec}s over limit'),
          _DataRow(
            label: 'Fine Amount',
            value: '${violation.fineAmount.toStringAsFixed(0)} EGP',
            valueStyle: TextStyle(
              color: AppColors.of(context).textPrimary,
              fontSize: 22,
              fontWeight: FontWeight.bold,
            ),
          ),

          Divider(color: AppColors.of(context).border, height: 28),

          // Timestamp row
          Row(
            children: [
              Icon(Icons.access_time_rounded,
                  size: 15, color: AppColors.of(context).textMuted),
              const SizedBox(width: 8),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('Date & Time',
                      style: TextStyle(
                          color: AppColors.of(context).textMuted,
                          fontSize: 12)),
                  Text('$dateStr • $timeStr',
                      style: TextStyle(
                          color: AppColors.of(context).textSecondary,
                          fontSize: 13)),
                ],
              ),
            ],
          ),

          const SizedBox(height: 12),

          // Coordinates row
          Row(
            children: [
              Icon(Icons.location_on_outlined,
                  size: 15, color: AppColors.of(context).textMuted),
              const SizedBox(width: 8),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('Coordinates',
                      style: TextStyle(
                          color: AppColors.of(context).textMuted,
                          fontSize: 12)),
                  Text(
                    '${violation.latitude.toStringAsFixed(4)}°N, '
                    '${violation.longitude.toStringAsFixed(4)}°E',
                    style: TextStyle(
                        color: AppColors.of(context).textSecondary,
                        fontSize: 13),
                  ),
                ],
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _DataRow extends StatelessWidget {
  final String label;
  final String value;
  final Color? valueColor;
  final TextStyle? valueStyle;

  const _DataRow({
    required this.label,
    required this.value,
    this.valueColor,
    this.valueStyle,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Expanded(
              child: Text(label,
                  style: TextStyle(
                      color: AppColors.of(context).textSecondary,
                      fontSize: 14))),
          const SizedBox(width: 12),
          Expanded(
              child: Text(
            value,
            textAlign: TextAlign.end,
            style: valueStyle ??
                TextStyle(
                  color: valueColor ?? AppColors.of(context).textPrimary,
                  fontSize: 15,
                  fontWeight: FontWeight.w500,
                ),
          )),
        ],
      ),
    );
  }
}

class _StatusBadge extends StatelessWidget {
  final bool isPaid;
  const _StatusBadge({required this.isPaid});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
      decoration: BoxDecoration(
        color: isPaid
            ? AppColors.success.withValues(alpha: 0.15)
            : AppColors.warning.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(6),
      ),
      child: Text(
        isPaid ? 'PAID' : 'UNPAID',
        style: TextStyle(
          color: isPaid ? AppColors.success : AppColors.warning,
          fontSize: 11,
          fontWeight: FontWeight.bold,
          letterSpacing: 0.5,
        ),
      ),
    );
  }
}

// ── Action area ───────────────────────────────────────────────────────────────

class _ActionArea extends StatelessWidget {
  final ViolationData violation;
  const _ActionArea({required this.violation});

  @override
  Widget build(BuildContext context) {
    if (violation.isPaid) {
      return Container(
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: AppColors.success.withValues(alpha: 0.12),
          border: Border.all(color: AppColors.success),
          borderRadius: BorderRadius.circular(12),
        ),
        child: const Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(Icons.check_circle_rounded,
                color: AppColors.success, size: 22),
            SizedBox(width: 8),
            Text(
              'This fine has been paid',
              style: TextStyle(
                  color: AppColors.success,
                  fontSize: 14,
                  fontWeight: FontWeight.w600),
            ),
          ],
        ),
      );
    }

    return Row(
      children: [
        // Dispute button
        Expanded(
          child: OutlinedButton(
            style: OutlinedButton.styleFrom(
              foregroundColor: AppColors.of(context).textSecondary,
              side: BorderSide(color: AppColors.of(context).border),
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(10),
              ),
            ),
            onPressed: () {
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(
                    content: Text('Dispute flow coming in Phase 6F')),
              );
            },
            child: const Text('Dispute'),
          ),
        ),
        const SizedBox(width: 12),
        // Pay Now button
        Expanded(
          child: ElevatedButton(
            style: ElevatedButton.styleFrom(
              backgroundColor: AppColors.danger,
              foregroundColor: AppColors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(10),
              ),
              elevation: 0,
            ),
            onPressed: () =>
                context.go('/payment/method?id=${violation.violationId}'),
            child: const Text(
              'Pay Fine (demo)',
              style: TextStyle(fontWeight: FontWeight.w600),
            ),
          ),
        ),
      ],
    );
  }
}
