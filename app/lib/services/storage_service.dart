// lib/services/storage_service.dart
// Phase 6D: Full Hive CRUD for ViolationData.
//
// IMPORTANT: Run build_runner BEFORE calling Hive.openBox<ViolationData>():
//   flutter pub run build_runner build --delete-conflicting-outputs
// This generates violation.g.dart which contains ViolationDataAdapter.

import 'package:flutter/foundation.dart';
import 'package:hive_flutter/hive_flutter.dart';
import '../models/violation.dart';

class StorageService {
  static const String _boxName = 'violations';

  // ── Initialisation ─────────────────────────────────────────────────────────

  /// Call once from main() after Hive.initFlutter().
  /// Registers the generated adapter and opens the box.
  static Future<void> init() async {
    await Hive.initFlutter();
    // Register only if not already registered (safe for hot restart)
    if (!Hive.isAdapterRegistered(0)) {
      Hive.registerAdapter(ViolationDataAdapter());
      debugPrint('[StorageService] ViolationDataAdapter registered');
    }
    await Hive.openBox<ViolationData>(_boxName);
    debugPrint('[StorageService] Box "$_boxName" opened');
  }

  // ── Box accessor ───────────────────────────────────────────────────────────

  Box<ViolationData> get _box => Hive.box<ViolationData>(_boxName);

  // ── CRUD ───────────────────────────────────────────────────────────────────

  /// Upsert: overwrite if the same violationId already exists.
  Future<void> saveViolation(ViolationData v) async {
    await _box.put(v.violationId, v);
    debugPrint('[StorageService] Saved violation ${v.violationId}');
  }

  /// Returns all violations sorted newest-first.
  Future<List<ViolationData>> getAllViolations() async {
    final list = _box.values.toList();
    list.sort((a, b) => b.timestamp.compareTo(a.timestamp));
    return list;
  }

  /// Single lookup: returns null if not found.
  Future<ViolationData?> getViolation(String violationId) async {
    return _box.get(violationId);
  }

  /// Mark a violation as paid and persist the change.
  Future<void> markPaid(String violationId) async {
    final v = _box.get(violationId);
    if (v != null) {
      v.isPaid = true;
      await v.save(); // HiveObject.save() writes back to the box
      debugPrint('[StorageService] Marked paid: $violationId');
    } else {
      debugPrint('[StorageService] markPaid: $violationId not found');
    }
  }

  /// Delete all violations from the box.
  Future<void> clearAll() async {
    await _box.clear();
    debugPrint('[StorageService] All violations cleared');
  }
}
