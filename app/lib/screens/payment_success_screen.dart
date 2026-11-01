// lib/screens/payment_success_screen.dart
// Phase 6F: Payment Success screen.
//
// Route: /payment/success?id=VIOLATION_ID
// On arrival: also calls markPaid() as safety (idempotent).
// "Back to Violations" → /violations
// "Go to Home" → /home

import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../models/violation.dart';
import '../providers/violations_provider.dart';
import '../theme/app_theme.dart';

class PaymentSuccessScreen extends StatefulWidget {
  final String violationId;
  const PaymentSuccessScreen({super.key, required this.violationId});

  @override
  State<PaymentSuccessScreen> createState() => _PaymentSuccessScreenState();
}

class _PaymentSuccessScreenState extends State<PaymentSuccessScreen>
    with SingleTickerProviderStateMixin {
  late final AnimationController _ctrl;
  late final Animation<double> _scaleAnim;
  late final Animation<double> _fadeAnim;

  // Staggered content animations
  late final Animation<double> _titleFade;
  late final Animation<double> _receiptFade;
  late final Animation<Offset> _receiptSlide;

  @override
  void initState() {
    super.initState();
    _ctrl = AnimationController(
        vsync: this, duration: const Duration(milliseconds: 900));

    _scaleAnim = CurvedAnimation(
      parent: _ctrl,
      curve: const Interval(0.0, 0.55, curve: Curves.elasticOut),
    );
    _fadeAnim = CurvedAnimation(
      parent: _ctrl,
      curve: const Interval(0.0, 0.45, curve: Curves.easeIn),
    );
    _titleFade = CurvedAnimation(
      parent: _ctrl,
      curve: const Interval(0.35, 0.65, curve: Curves.easeIn),
    );
    _receiptFade = CurvedAnimation(
      parent: _ctrl,
      curve: const Interval(0.5, 0.85, curve: Curves.easeIn),
    );
    _receiptSlide =
        Tween<Offset>(begin: const Offset(0, 0.25), end: Offset.zero)
            .animate(CurvedAnimation(
      parent: _ctrl,
      curve: const Interval(0.5, 0.85, curve: Curves.easeOut),
    ));

    _ctrl.forward();

    // Safety: mark paid (idempotent)
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      if (mounted) {
        await context.read<ViolationsProvider>().markPaid(widget.violationId);
      }
    });
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final provider = context.watch<ViolationsProvider>();
    final ViolationData? violation = provider.violations
        .where((v) => v.violationId == widget.violationId)
        .firstOrNull;

    final shortId = () {
      final parts = widget.violationId.split('_');
      return parts.length >= 3 ? parts.last : widget.violationId;
    }();

    final datePaid = DateFormat('dd MMM yyyy').format(DateTime.now());

    return Scaffold(
      backgroundColor: AppColors.of(context).bgDark,
      body: SafeArea(
        child: Column(
          children: [
            Expanded(
              child: SingleChildScrollView(
                padding:
                    const EdgeInsets.symmetric(horizontal: 24, vertical: 32),
                child: Column(
                  children: [
                    // ── Check circle ────────────────────────────────────
                    ScaleTransition(
                      scale: _scaleAnim,
                      child: FadeTransition(
                        opacity: _fadeAnim,
                        child: _CheckCircle(),
                      ),
                    ),

                    const SizedBox(height: 24),

                    // ── Title ───────────────────────────────────────────
                    FadeTransition(
                      opacity: _titleFade,
                      child: Column(
                        children: [
                          Text(
                            'Demo payment complete',
                            style: TextStyle(
                                color: AppColors.of(context).textPrimary,
                                fontSize: 24,
                                fontWeight: FontWeight.bold),
                          ),
                          const SizedBox(height: 6),
                          Text(
                            'No money was charged. Payment is a demo screen.',
                            style: TextStyle(
                                color: AppColors.of(context).textSecondary,
                                fontSize: 14),
                          ),
                        ],
                      ),
                    ),

                    const SizedBox(height: 32),

                    Divider(color: AppColors.of(context).border),

                    const SizedBox(height: 24),

                    // ── Receipt card ────────────────────────────────────
                    SlideTransition(
                      position: _receiptSlide,
                      child: FadeTransition(
                        opacity: _receiptFade,
                        child: _ReceiptCard(
                          violationId: widget.violationId,
                          shortId: shortId,
                          violation: violation,
                          datePaid: datePaid,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),

            // ── Action buttons ──────────────────────────────────────────
            Padding(
              padding: const EdgeInsets.fromLTRB(24, 0, 24, 24),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  ElevatedButton(
                    style: ElevatedButton.styleFrom(
                      backgroundColor: AppColors.danger,
                      foregroundColor: AppColors.white,
                      padding: const EdgeInsets.symmetric(vertical: 15),
                      shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(12)),
                      elevation: 2,
                    ),
                    onPressed: () => context.go('/violations'),
                    child: const Text('Back to Violations',
                        style: TextStyle(
                            fontSize: 15, fontWeight: FontWeight.w600)),
                  ),
                  const SizedBox(height: 10),
                  TextButton(
                    style: TextButton.styleFrom(
                      foregroundColor: AppColors.of(context).textSecondary,
                      padding: const EdgeInsets.symmetric(vertical: 12),
                    ),
                    onPressed: () => context.go('/home'),
                    child: const Text('Go to Home',
                        style: TextStyle(fontSize: 14)),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Check circle ──────────────────────────────────────────────────────────────

class _CheckCircle extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Container(
      width: 96,
      height: 96,
      decoration: BoxDecoration(
        color: AppColors.success.withValues(alpha: 0.18),
        shape: BoxShape.circle,
      ),
      child: const Center(
        child: Icon(
          Icons.check_circle_rounded,
          color: AppColors.success,
          size: 64,
        ),
      ),
    );
  }
}

// ── Receipt card ──────────────────────────────────────────────────────────────

class _ReceiptCard extends StatelessWidget {
  final String violationId;
  final String shortId;
  final ViolationData? violation;
  final String datePaid;

  const _ReceiptCard({
    required this.violationId,
    required this.shortId,
    required this.violation,
    required this.datePaid,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppColors.of(context).bgCard,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: AppColors.of(context).border),
        boxShadow: [
          BoxShadow(
              color: AppColors.black.withValues(alpha: 0.2),
              blurRadius: 12,
              offset: const Offset(0, 4)),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.receipt_long_rounded,
                  color: AppColors.of(context).primary, size: 18),
              const SizedBox(width: 8),
              Text(
                'Receipt',
                style: TextStyle(
                    color: AppColors.of(context).textPrimary,
                    fontSize: 16,
                    fontWeight: FontWeight.w600),
              ),
            ],
          ),
          const SizedBox(height: 16),
          Divider(color: AppColors.of(context).border, height: 1),
          const SizedBox(height: 14),
          _ReceiptRow(label: 'Violation ID', value: '#$shortId'),
          if (violation != null) ...[
            _ReceiptRow(
              label: 'Amount Paid',
              value: 'EGP ${violation!.fineAmount.toStringAsFixed(2)}',
              valueColor: AppColors.of(context).primary,
              valueFontSize: 18,
              valueBold: true,
            ),
          ],
          _ReceiptRow(label: 'Date Paid', value: datePaid),
          const SizedBox(height: 10),
          Divider(color: AppColors.of(context).border, height: 1),
          const SizedBox(height: 10),
          const Row(
            children: [
              Icon(Icons.check_circle_rounded,
                  color: AppColors.success, size: 16),
              SizedBox(width: 6),
              Text(
                'Fine cleared',
                style: TextStyle(
                    color: AppColors.success,
                    fontSize: 13,
                    fontWeight: FontWeight.w600),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _ReceiptRow extends StatelessWidget {
  final String label;
  final String value;
  final Color? valueColor;
  final double valueFontSize;
  final bool valueBold;

  const _ReceiptRow({
    required this.label,
    required this.value,
    this.valueColor,
    this.valueFontSize = 14,
    this.valueBold = false,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Expanded(
              child: Text(
            label,
            style: TextStyle(
                color: AppColors.of(context).textSecondary, fontSize: 13),
          )),
          const SizedBox(width: 12),
          Expanded(
              child: Text(
            value,
            textAlign: TextAlign.end,
            style: TextStyle(
              color: valueColor ?? AppColors.of(context).textPrimary,
              fontSize: valueFontSize,
              fontWeight: valueBold ? FontWeight.bold : FontWeight.w500,
            ),
          )),
        ],
      ),
    );
  }
}
