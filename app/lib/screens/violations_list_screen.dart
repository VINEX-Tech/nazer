// lib/screens/violations_list_screen.dart
// Phase 6D: Full implementation matching ViolationsListScreen.tsx design.
//
// Fix: replaced '../theme/app_colors.dart' with '../theme/app_theme.dart'
//      (AppColors is defined in app_theme.dart, no separate app_colors.dart exists).

import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:provider/provider.dart';
import 'package:intl/intl.dart';

import '../models/violation.dart';
import '../widgets/crash_history.dart';
import '../providers/violations_provider.dart';
import '../theme/app_theme.dart';

class ViolationsListScreen extends StatefulWidget {
  const ViolationsListScreen({super.key});

  @override
  State<ViolationsListScreen> createState() => _ViolationsListScreenState();
}

class _ViolationsListScreenState extends State<ViolationsListScreen> {
  _Filter _filter = _Filter.all;

  @override
  Widget build(BuildContext context) {
    return Consumer<ViolationsProvider>(
      builder: (context, provider, _) {
        final all = provider.violations;
        final filtered = _applyFilter(all);
        final unpaid = all.where((v) => !v.isPaid).toList();
        final totalUnpaid = unpaid.fold(0.0, (s, v) => s + v.fineAmount);

        return Scaffold(
          backgroundColor: AppColors.of(context).bgDark,
          body: SafeArea(
            child: RefreshIndicator(
              color: AppColors.of(context).primary,
              backgroundColor: AppColors.of(context).bgCard,
              onRefresh: provider.load,
              child: CustomScrollView(
                physics: const AlwaysScrollableScrollPhysics(),
                slivers: [
                  // ── Header ───────────────────────────────────────────────
                  SliverToBoxAdapter(
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(16, 16, 16, 12),
                      child: Text(
                        'Alerts',
                        style: TextStyle(
                          color: AppColors.of(context).textPrimary,
                          fontSize: 20,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ),
                  ),

                  const SliverToBoxAdapter(
                      child: Padding(
                          padding: EdgeInsets.all(16), child: CrashHistory())),
                  // ── Summary card ─────────────────────────────────────────
                  SliverToBoxAdapter(
                    child: Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 16),
                      child: _SummaryCard(
                        totalUnpaid: totalUnpaid,
                        unpaidCount: unpaid.length,
                        onPayAll: unpaid.isEmpty
                            ? null
                            : () => context.go(
                                '/payment/method?id=${unpaid.first.violationId}'),
                      ),
                    ),
                  ),

                  const SliverToBoxAdapter(child: SizedBox(height: 16)),

                  // ── Filter chips ─────────────────────────────────────────
                  SliverToBoxAdapter(
                    child: Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 16),
                      child: Wrap(
                        runSpacing: 8,
                        children: _Filter.values
                            .map((f) => Padding(
                                  padding: const EdgeInsets.only(right: 8),
                                  child: _FilterChip(
                                    label: f.label,
                                    active: _filter == f,
                                    onTap: () => setState(() => _filter = f),
                                  ),
                                ))
                            .toList(),
                      ),
                    ),
                  ),

                  const SliverToBoxAdapter(child: SizedBox(height: 12)),

                  // ── List or empty state ──────────────────────────────────
                  if (filtered.isEmpty)
                    SliverFillRemaining(
                      hasScrollBody: false,
                      child: _EmptyState(filter: _filter),
                    )
                  else
                    SliverPadding(
                      padding: const EdgeInsets.fromLTRB(16, 0, 16, 24),
                      sliver: SliverList(
                        delegate: SliverChildBuilderDelegate(
                          (ctx, i) => Padding(
                            padding: const EdgeInsets.only(bottom: 12),
                            child: _ViolationCard(
                              violation: filtered[i],
                              onTap: () => context
                                  .go('/violations/${filtered[i].violationId}'),
                              onPayNow: filtered[i].isPaid
                                  ? null
                                  : () => context.go(
                                      '/payment/method?id=${filtered[i].violationId}'),
                            ),
                          ),
                          childCount: filtered.length,
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }

  List<ViolationData> _applyFilter(List<ViolationData> all) {
    switch (_filter) {
      case _Filter.all:
        return all;
      case _Filter.unpaid:
        return all.where((v) => !v.isPaid).toList();
      case _Filter.paid:
        return all.where((v) => v.isPaid).toList();
    }
  }
}

// ── Enums ─────────────────────────────────────────────────────────────────────

enum _Filter {
  all('All'),
  unpaid('Unpaid'),
  paid('Paid');

  const _Filter(this.label);
  final String label;
}

// ── Summary card ──────────────────────────────────────────────────────────────

class _SummaryCard extends StatelessWidget {
  final double totalUnpaid;
  final int unpaidCount;
  final VoidCallback? onPayAll;

  const _SummaryCard({
    required this.totalUnpaid,
    required this.unpaidCount,
    required this.onPayAll,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgCard,
        border: Border.all(color: AppColors.of(context).border),
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Total Unpaid',
            style: TextStyle(
              color: AppColors.of(context).textSecondary,
              fontSize: 13,
            ),
          ),
          const SizedBox(height: 4),
          Text(
            '${totalUnpaid.toStringAsFixed(0)} EGP',
            style: TextStyle(
              color: AppColors.of(context).textPrimary,
              fontSize: 34,
              fontWeight: FontWeight.bold,
            ),
          ),
          const SizedBox(height: 4),
          Text(
            '$unpaidCount violation${unpaidCount != 1 ? 's' : ''}',
            style: TextStyle(
                color: AppColors.of(context).textSecondary, fontSize: 13),
          ),
          const SizedBox(height: 16),
          SizedBox(
            width: double.infinity,
            child: TextButton(
              style: TextButton.styleFrom(
                backgroundColor: AppColors.of(context).bgSurface,
                foregroundColor: AppColors.of(context).danger,
                padding: const EdgeInsets.symmetric(vertical: 12),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(10),
                ),
              ),
              onPressed: onPayAll,
              child: const Text(
                'Pay next fine',
                style: TextStyle(fontWeight: FontWeight.w600, fontSize: 15),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ── Filter chip ───────────────────────────────────────────────────────────────

class _FilterChip extends StatelessWidget {
  final String label;
  final bool active;
  final VoidCallback onTap;

  const _FilterChip({
    required this.label,
    required this.active,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 150),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        decoration: BoxDecoration(
          color: active ? AppColors.of(context).primary : AppColors.transparent,
          borderRadius: BorderRadius.circular(20),
          border: Border.all(
            color: active
                ? AppColors.of(context).primary
                : AppColors.of(context).border,
          ),
        ),
        child: Text(
          label,
          style: TextStyle(
            color: active
                ? AppColors.of(context).bgCard
                : AppColors.of(context).textSecondary,
            fontSize: 13,
            fontWeight: active ? FontWeight.w600 : FontWeight.normal,
          ),
        ),
      ),
    );
  }
}

// ── Violation row card ────────────────────────────────────────────────────────

class _ViolationCard extends StatelessWidget {
  final ViolationData violation;
  final VoidCallback onTap;
  final VoidCallback? onPayNow;

  const _ViolationCard({
    required this.violation,
    required this.onTap,
    required this.onPayNow,
  });

  @override
  Widget build(BuildContext context) {
    final dateStr = DateFormat('MMM d, yyyy').format(violation.timestamp);
    final timeStr = DateFormat('h:mm a').format(violation.timestamp);

    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: AppColors.of(context).bgCard,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: AppColors.of(context).border),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Top row: icon + type + amount
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Red alert icon
                Container(
                  width: 44,
                  height: 44,
                  decoration: BoxDecoration(
                    color: AppColors.of(context).danger.withValues(alpha: 0.15),
                    shape: BoxShape.circle,
                  ),
                  child: Icon(Icons.warning_rounded,
                      color: AppColors.of(context).danger, size: 22),
                ),
                const SizedBox(width: 12),
                // Speed / zone text
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Speed Violation',
                        style: TextStyle(
                          color: AppColors.of(context).textPrimary,
                          fontSize: 15,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        '${violation.speed.toStringAsFixed(0)} km/h'
                        ' in ${violation.speedLimit} km/h zone',
                        style: TextStyle(
                            color: AppColors.of(context).textSecondary,
                            fontSize: 13),
                      ),
                    ],
                  ),
                ),
                // Fine amount
                Text(
                  '${violation.fineAmount.toStringAsFixed(0)} EGP',
                  style: TextStyle(
                    color: AppColors.of(context).textPrimary,
                    fontSize: 16,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),

            const SizedBox(height: 12),

            // Date / time row
            Row(
              children: [
                Icon(Icons.access_time_rounded,
                    size: 13, color: AppColors.of(context).textMuted),
                const SizedBox(width: 4),
                Text(
                  '$dateStr • $timeStr',
                  style: TextStyle(
                      color: AppColors.of(context).textMuted, fontSize: 12),
                ),
              ],
            ),

            const SizedBox(height: 12),

            // Status badge + Pay Now button
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                _StatusBadge(isPaid: violation.isPaid),
                if (onPayNow != null)
                  GestureDetector(
                    onTap: onPayNow,
                    child: Text(
                      'Pay Now',
                      style: TextStyle(
                        color: AppColors.of(context).danger,
                        fontSize: 13,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

// ── Status badge ──────────────────────────────────────────────────────────────

class _StatusBadge extends StatelessWidget {
  final bool isPaid;
  const _StatusBadge({required this.isPaid});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
      decoration: BoxDecoration(
        color: isPaid
            ? AppColors.of(context).success.withValues(alpha: 0.15)
            : AppColors.of(context).warning.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(6),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (isPaid) ...[
            Icon(Icons.check_circle_outline,
                size: 12, color: AppColors.of(context).success),
            const SizedBox(width: 4),
          ],
          Text(
            isPaid ? 'PAID' : 'UNPAID',
            style: TextStyle(
              color: isPaid
                  ? AppColors.of(context).success
                  : AppColors.of(context).warning,
              fontSize: 11,
              fontWeight: FontWeight.bold,
              letterSpacing: 0.5,
            ),
          ),
        ],
      ),
    );
  }
}

// ── Empty state ───────────────────────────────────────────────────────────────

class _EmptyState extends StatelessWidget {
  final _Filter filter;
  const _EmptyState({required this.filter});

  @override
  Widget build(BuildContext context) {
    final isPaidFilter = filter == _Filter.paid;
    final isUnpaidFilter = filter == _Filter.unpaid;

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(40),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              isPaidFilter ? '📋' : '🎉',
              style: const TextStyle(fontSize: 56),
            ),
            const SizedBox(height: 16),
            Text(
              isUnpaidFilter
                  ? 'All fines are paid!'
                  : isPaidFilter
                      ? 'No paid violations yet'
                      : 'No violations recorded',
              style: TextStyle(
                color: AppColors.of(context).textPrimary,
                fontSize: 20,
                fontWeight: FontWeight.w600,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 8),
            Text(
              isUnpaidFilter
                  ? 'Great job keeping it within the limit.'
                  : 'Keep up the safe driving!',
              style: TextStyle(
                  color: AppColors.of(context).textSecondary, fontSize: 14),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}
