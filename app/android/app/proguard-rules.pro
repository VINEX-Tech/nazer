# android/app/proguard-rules.pro
# Phase 6H: NAZER release build keep rules

# ── Flutter ───────────────────────────────────────────────────────────────────
-keep class io.flutter.** { *; }
-keep class io.flutter.plugins.** { *; }
-dontwarn io.flutter.**

# ── Hive ──────────────────────────────────────────────────────────────────────
# Keep all HiveObject subclasses and their generated adapters
-keep class * extends com.hivedb.hive.HiveObject { *; }
-keep class **$HiveAdapter { *; }
# Hive uses reflection to find TypeAdapters
-keepnames class * extends com.hivedb.hive.TypeAdapter
-keep @com.hivedb.hive.annotations.HiveType class * { *; }
-keep @com.hivedb.hive.annotations.HiveField class * { *; }
# dart2java layer
-keep class dev.flutter.pigeon.** { *; }

# ── flutter_blue_plus ─────────────────────────────────────────────────────────
-keep class com.boskokg.flutter_blue_plus.** { *; }
-dontwarn com.boskokg.flutter_blue_plus.**

# ── flutter_local_notifications ───────────────────────────────────────────────
-keep class com.dexterous.flutterlocalnotifications.** { *; }
-dontwarn com.dexterous.flutterlocalnotifications.**
# Keep notification receiver and service classes
-keep class * extends android.app.Service
-keep class * extends android.content.BroadcastReceiver

# ── go_router (dart: no Android classes; keep for completeness) ──────────────
# go_router is pure Dart: no Android keep rules needed.

# ── Gson / JSON (used by some plugins internally) ─────────────────────────────
-keepattributes Signature
-keepattributes *Annotation*
-dontwarn sun.misc.**

# ── General Android safety ────────────────────────────────────────────────────
-keepclassmembers class * implements android.os.Parcelable {
    static ** CREATOR;
}
-keepclassmembers enum * {
    public static **[] values();
    public static ** valueOf(java.lang.String);
}