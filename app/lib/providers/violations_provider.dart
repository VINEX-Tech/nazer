// lib/providers/violations_provider.dart
// Phase 6D: Full implementation: Hive persistence + notification callback.
// Phase 6E: Added clearAll().
//
// NotificationService (notification_service.dart) is injected via
// [onViolationAdded] callback to avoid a hard dependency on the notification
// package inside this provider.

import 'package:flutter/foundation.dart';
import '../models/violation.dart';
import '../services/storage_service.dart';

class ViolationsProvider extends ChangeNotifier {
  final StorageService _storage;

  /// Optional callback: called after every new violation is persisted.
  /// Wire this to NotificationService.showViolationNotification() in main.dart.
  void Function(ViolationData)? onViolationAdded;

  ViolationsProvider(this._storage) {
    // Auto-load persisted violations on construction so the UI is
    // populated immediately (even before BLE delivers anything new).
    _loadInternal();
  }

  // ── State ──────────────────────────────────────────────────────────────────

  List<ViolationData> _violations = [];

  List<ViolationData> get violations => List.unmodifiable(_violations);

  int get unpaidCount => _violations.where((v) => !v.isPaid).length;

  double get totalUnpaidAmount =>
      _violations.where((v) => !v.isPaid).fold(0.0, (s, v) => s + v.fineAmount);

  // ── Public API ─────────────────────────────────────────────────────────────

  /// Reload all violations from Hive and notify listeners.
  /// Call this for pull-to-refresh.
  Future<void> load() async {
    await _loadInternal();
  }

  /// Called by DeviceProvider whenever a violation arrives over BLE.
  /// Saves to Hive, reloads the list, and fires the notification callback.
  Future<void> addViolation(ViolationData v) async {
    await _storage.saveViolation(v);
    await _loadInternal();
    debugPrint('[ViolationsProvider] Violation saved: ${v.violationId}');
    // Fire notification (if wired up by main.dart)
    onViolationAdded?.call(v);
  }

  /// Mark a violation as paid, persist, and reload.
  Future<void> markPaid(String violationId) async {
    await _storage.markPaid(violationId);
    await _loadInternal();
    debugPrint('[ViolationsProvider] Marked paid: $violationId');
  }

  /// Delete all violation records (debug / settings action).
  Future<void> clearAll() async {
    await _storage.clearAll();
    await _loadInternal();
    debugPrint('[ViolationsProvider] All violations cleared');
  }

  // ── Private ────────────────────────────────────────────────────────────────

  Future<void> _loadInternal() async {
    _violations = await _storage.getAllViolations();
    notifyListeners();
  }
}
