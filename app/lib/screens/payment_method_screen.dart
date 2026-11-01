// lib/screens/payment_method_screen.dart
// Phase 6F: Payment Method selection screen.
//
// Route: /payment/method?id=VIOLATION_ID
// Receives violationId from GoRouterState.uri.queryParameters['id'].
// Navigates to /payment/form?id=X&method=card|wallet on Continue.

import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../models/violation.dart';
import '../providers/violations_provider.dart';
import '../theme/app_theme.dart';

class PaymentMethodScreen extends StatefulWidget {
  final String violationId;
  const PaymentMethodScreen({super.key, required this.violationId});

  @override
  State<PaymentMethodScreen> createState() => _PaymentMethodScreenState();
}

class _PaymentMethodScreenState extends State<PaymentMethodScreen> {
  String? _selected; // 'card' | 'wallet'

  @override
  Widget build(BuildContext context) {
    final provider = context.watch<ViolationsProvider>();
    final ViolationData? violation = provider.violations
        .where((v) => v.violationId == widget.violationId)
        .firstOrNull;

    // ── Not found or already paid ──────────────────────────────────────────
    if (violation == null || violation.isPaid) {
      return Scaffold(
        backgroundColor: AppColors.of(context).bgDark,
        appBar: AppBar(
          backgroundColor: AppColors.of(context).bgDark,
          leading: IconButton(
            icon: Icon(Icons.arrow_back_ios_new_rounded,
                color: AppColors.of(context).textPrimary, size: 20),
            onPressed: () => context.go('/violations'),
          ),
          title: Text('Pay Fine (demo)',
              style: TextStyle(
                  color: AppColors.of(context).textPrimary,
                  fontWeight: FontWeight.w600)),
          elevation: 0,
        ),
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(32),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  violation?.isPaid == true
                      ? Icons.check_circle_outline_rounded
                      : Icons.error_outline_rounded,
                  color: violation?.isPaid == true
                      ? AppColors.success
                      : AppColors.danger,
                  size: 64,
                ),
                const SizedBox(height: 16),
                Text(
                  violation?.isPaid == true
                      ? 'Already Paid'
                      : 'Violation Not Found',
                  style: TextStyle(
                      color: AppColors.of(context).textPrimary,
                      fontSize: 20,
                      fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 8),
                Text(
                  violation?.isPaid == true
                      ? 'This fine has already been paid.'
                      : 'The requested violation could not be found.',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                      color: AppColors.of(context).textSecondary, fontSize: 14),
                ),
                const SizedBox(height: 32),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton(
                    style: ElevatedButton.styleFrom(
                      backgroundColor: AppColors.of(context).primary,
                      foregroundColor: AppColors.of(context).bgDark,
                      padding: const EdgeInsets.symmetric(vertical: 14),
                      shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(10)),
                    ),
                    onPressed: () => context.go('/violations'),
                    child: const Text('Back to Violations',
                        style: TextStyle(fontWeight: FontWeight.w600)),
                  ),
                ),
              ],
            ),
          ),
        ),
      );
    }

    // ── Normal flow ────────────────────────────────────────────────────────
    final dateStr =
        DateFormat('dd MMM yyyy • HH:mm').format(violation.timestamp);

    return Scaffold(
      backgroundColor: AppColors.of(context).bgDark,
      body: SafeArea(
        child: Column(
          children: [
            // Header
            _Header(onBack: () => context.pop()),

            // Body
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    // ── Violation summary card ──────────────────────────
                    _ViolationSummaryCard(
                        violation: violation, dateStr: dateStr),

                    const SizedBox(height: 24),

                    Text(
                      'Select Payment Method',
                      style: TextStyle(
                          color: AppColors.of(context).textSecondary,
                          fontSize: 13,
                          fontWeight: FontWeight.w500,
                          letterSpacing: 0.4),
                    ),

                    const SizedBox(height: 12),

                    // ── Method cards ────────────────────────────────────
                    _MethodCard(
                      id: 'card',
                      icon: Icons.credit_card_rounded,
                      iconColor: AppColors.info,
                      title: 'Credit / Debit Card',
                      subtitle: 'Visa, Mastercard',
                      selected: _selected == 'card',
                      onTap: () => setState(() => _selected = 'card'),
                    ),
                    const SizedBox(height: 12),
                    _MethodCard(
                      id: 'wallet',
                      icon: Icons.account_balance_wallet_rounded,
                      iconColor: AppColors.success,
                      title: 'Digital Wallet',
                      subtitle: 'InstaPay, Vodafone Cash, Meeza',
                      selected: _selected == 'wallet',
                      onTap: () => setState(() => _selected = 'wallet'),
                    ),

                    const SizedBox(height: 32),

                    // ── Continue button ─────────────────────────────────
                    _ContinueButton(
                      enabled: _selected != null,
                      onPressed: _selected == null
                          ? null
                          : () => context.go('/payment/form'
                              '?id=${widget.violationId}'
                              '&method=$_selected'),
                    ),

                    const SizedBox(height: 16),

                    // ── Security notice ─────────────────────────────────
                    Container(
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: AppColors.of(context).bgCard,
                        borderRadius: BorderRadius.circular(10),
                        border: Border.all(color: AppColors.of(context).border),
                      ),
                      child: Row(
                        children: [
                          Icon(Icons.lock_outline_rounded,
                              color: AppColors.of(context).textMuted, size: 18),
                          const SizedBox(width: 10),
                          Expanded(
                            child: Text(
                              'Your payment information is encrypted and secure. '
                              'No card details are stored.',
                              style: TextStyle(
                                  color: AppColors.of(context).textMuted,
                                  fontSize: 12),
                            ),
                          ),
                        ],
                      ),
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
  final VoidCallback onBack;
  const _Header({required this.onBack});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(4, 8, 16, 8),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgDark,
        border: Border(bottom: BorderSide(color: AppColors.of(context).border)),
      ),
      child: Row(
        children: [
          IconButton(
            icon: Icon(Icons.arrow_back_ios_new_rounded,
                color: AppColors.of(context).textPrimary, size: 20),
            onPressed: onBack,
          ),
          Text(
            'Pay Fine (demo)',
            style: TextStyle(
                color: AppColors.of(context).textPrimary,
                fontSize: 18,
                fontWeight: FontWeight.w600),
          ),
        ],
      ),
    );
  }
}

// ── Violation summary card ────────────────────────────────────────────────────

class _ViolationSummaryCard extends StatelessWidget {
  final ViolationData violation;
  final String dateStr;
  const _ViolationSummaryCard({required this.violation, required this.dateStr});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgCard,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: AppColors.of(context).border),
        boxShadow: [
          BoxShadow(
              color: AppColors.black.withValues(alpha: 0.2),
              blurRadius: 10,
              offset: const Offset(0, 4)),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Speed badge
              Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                decoration: BoxDecoration(
                  color: AppColors.danger.withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(
                      color: AppColors.danger.withValues(alpha: 0.4)),
                ),
                child: Column(
                  children: [
                    Text(
                      violation.speed.toStringAsFixed(0),
                      style: const TextStyle(
                          color: AppColors.danger,
                          fontSize: 28,
                          fontWeight: FontWeight.bold,
                          height: 1),
                    ),
                    const Text(
                      'km/h',
                      style: TextStyle(
                          color: AppColors.danger,
                          fontSize: 11,
                          fontWeight: FontWeight.w500),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'in a ${violation.speedLimit} km/h zone',
                      style: TextStyle(
                          color: AppColors.of(context).textSecondary,
                          fontSize: 14),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      'EGP ${violation.fineAmount.toStringAsFixed(2)}',
                      style: TextStyle(
                          color: AppColors.of(context).primary,
                          fontSize: 26,
                          fontWeight: FontWeight.bold),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Divider(color: AppColors.of(context).border, height: 1),
          const SizedBox(height: 10),
          Row(
            children: [
              Icon(Icons.access_time_rounded,
                  size: 14, color: AppColors.of(context).textMuted),
              const SizedBox(width: 6),
              Text(dateStr,
                  style: TextStyle(
                      color: AppColors.of(context).textMuted, fontSize: 12)),
            ],
          ),
        ],
      ),
    );
  }
}

// ── Method card ───────────────────────────────────────────────────────────────

class _MethodCard extends StatelessWidget {
  final String id;
  final IconData icon;
  final Color iconColor;
  final String title;
  final String subtitle;
  final bool selected;
  final VoidCallback onTap;

  const _MethodCard({
    required this.id,
    required this.icon,
    required this.iconColor,
    required this.title,
    required this.subtitle,
    required this.selected,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 200),
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: AppColors.of(context).bgCard,
          borderRadius: BorderRadius.circular(14),
          border: Border.all(
            color: selected
                ? AppColors.of(context).primary
                : AppColors.of(context).border,
            width: selected ? 2 : 1,
          ),
          boxShadow: selected
              ? [
                  BoxShadow(
                      color:
                          AppColors.of(context).primary.withValues(alpha: 0.15),
                      blurRadius: 8,
                      offset: const Offset(0, 2))
                ]
              : [],
        ),
        child: Row(
          children: [
            // Icon circle
            Container(
              width: 48,
              height: 48,
              decoration: BoxDecoration(
                color: iconColor.withValues(alpha: 0.12),
                shape: BoxShape.circle,
              ),
              child: Icon(icon, color: iconColor, size: 24),
            ),
            const SizedBox(width: 14),
            // Labels
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(title,
                      style: TextStyle(
                          color: AppColors.of(context).textPrimary,
                          fontSize: 15,
                          fontWeight: FontWeight.w600)),
                  const SizedBox(height: 2),
                  Text(subtitle,
                      style: TextStyle(
                          color: AppColors.of(context).textSecondary,
                          fontSize: 12)),
                ],
              ),
            ),
            // Radio circle
            AnimatedContainer(
              duration: const Duration(milliseconds: 200),
              width: 22,
              height: 22,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                border: Border.all(
                    color: selected
                        ? AppColors.of(context).primary
                        : AppColors.of(context).border,
                    width: 2),
                color: selected
                    ? AppColors.of(context).primary
                    : AppColors.transparent,
              ),
              child: selected
                  ? Icon(Icons.check_rounded,
                      color: AppColors.of(context).bgDark, size: 14)
                  : null,
            ),
          ],
        ),
      ),
    );
  }
}

// ── Continue button ───────────────────────────────────────────────────────────

class _ContinueButton extends StatelessWidget {
  final bool enabled;
  final VoidCallback? onPressed;
  const _ContinueButton({required this.enabled, required this.onPressed});

  @override
  Widget build(BuildContext context) {
    return AnimatedOpacity(
      duration: const Duration(milliseconds: 200),
      opacity: enabled ? 1.0 : 0.45,
      child: SizedBox(
        width: double.infinity,
        child: ElevatedButton(
          style: ElevatedButton.styleFrom(
            backgroundColor:
                enabled ? AppColors.danger : AppColors.of(context).bgSurface,
            foregroundColor: AppColors.white,
            padding: const EdgeInsets.symmetric(vertical: 16),
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            elevation: enabled ? 2 : 0,
          ),
          onPressed: onPressed,
          child: const Text('Continue',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
        ),
      ),
    );
  }
}
