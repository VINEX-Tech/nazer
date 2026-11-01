import 'package:flutter/material.dart';

class AppColors {
  static AppPalette of(BuildContext context) =>
      AppPalette(Theme.of(context).brightness == Brightness.dark);
  static const Color roadSlow = Color(0xFF22C55E);
  static const Color roadLocal = Color(0xFF3B82F6);
  static const Color roadMain = Color(0xFFEAB308);
  static const Color roadFast = Color(0xFFF97316);
  static const Color roadMotorway = Color(0xFFEF4444);
  static const Color white = Color(0xFFFFFFFF);
  static const Color black = Color(0xFF000000);
  static const Color transparent = Color(0x00000000);
  static const Color primary = Color(0xFFD7262A);
  static const Color primaryDark = Color(0xFF9B2A1E);
  static const Color accent = Color(0xFFEE5A24);
  static const Color brandForeground = Color(0xFFFF956D);
  static const Color bgDark = Color(0xFF121212);
  static const Color bgCard = Color(0xFF1D1D1D);
  static const Color bgSurface = Color(0xFF2A2A2A);
  static const Color bgLight = Color(0xFFF5F5F5);
  static const Color bgCardLight = Color(0xFFFFFFFF);
  static const Color bgSurfaceLight = Color(0xFFEBEBEB);
  static const Color success = Color(0xFF64D99A);
  static const Color successLight = Color(0xFF167447);
  static const Color warning = Color(0xFFFFC46B);
  static const Color warningLight = Color(0xFF895300);
  static const Color danger = Color(0xFFFF7079);
  static const Color dangerLight = Color(0xFFB91C35);
  static const Color info = Color(0xFFFF956D);
  static const Color textPrimary = Color(0xFFFAFAFA);
  static const Color textSecondary = Color(0xFFC3C3C3);
  static const Color textMuted = Color(0xFFA3A3A3);
  static const Color textPrimaryLight = Color(0xFF202020);
  static const Color textSecondaryLight = Color(0xFF555555);
  static const Color textMutedLight = Color(0xFF656565);
  static const Color border = Color(0xFF454545);
  static const Color borderLight = Color(0xFFCBCBCB);
  static const brandGradient = LinearGradient(colors: [primary, accent]);
}

// ── Theme builder ─────────────────────────────────────────────────────────────

class AppTheme {
  // ── Dark ──────────────────────────────────────────────────────────────────
  static ThemeData dark() {
    return ThemeData(
      brightness: Brightness.dark,
      scaffoldBackgroundColor: AppColors.bgDark,
      primaryColor: AppColors.brandForeground,
      colorScheme: const ColorScheme.dark(
        primary: AppColors.brandForeground,
        secondary: AppColors.accent,
        surface: AppColors.bgCard,
        error: AppColors.danger,
        onPrimary: AppColors.bgDark,
        onSecondary: AppColors.bgDark,
        onSurface: AppColors.textPrimary,
        onError: AppColors.bgDark,
      ),
      textTheme: const TextTheme(
        displayLarge: TextStyle(
            fontSize: 48,
            fontWeight: FontWeight.w700,
            color: AppColors.textPrimary),
        displayMedium: TextStyle(
            fontSize: 36,
            fontWeight: FontWeight.w700,
            color: AppColors.textPrimary),
        headlineLarge: TextStyle(
            fontSize: 24,
            fontWeight: FontWeight.w600,
            color: AppColors.textPrimary),
        headlineMedium: TextStyle(
            fontSize: 20,
            fontWeight: FontWeight.w600,
            color: AppColors.textPrimary),
        titleLarge: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w600,
            color: AppColors.textPrimary),
        bodyLarge: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w400,
            color: AppColors.textPrimary),
        bodyMedium: TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w400,
            color: AppColors.textSecondary),
        bodySmall: TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w400,
            color: AppColors.textMuted),
        labelLarge: TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w600,
            color: AppColors.brandForeground),
      ),
      cardTheme: const CardThemeData(
        color: AppColors.bgCard,
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.all(Radius.circular(16)),
          side: BorderSide(color: AppColors.border),
        ),
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: AppColors.bgDark,
        elevation: 0,
        centerTitle: false,
        titleTextStyle: TextStyle(
          fontSize: 18,
          fontWeight: FontWeight.w600,
          color: AppColors.textPrimary,
        ),
        iconTheme: IconThemeData(color: AppColors.textPrimary),
      ),
      bottomNavigationBarTheme: const BottomNavigationBarThemeData(
        backgroundColor: AppColors.bgCard,
        selectedItemColor: AppColors.brandForeground,
        unselectedItemColor: AppColors.textMuted,
        type: BottomNavigationBarType.fixed,
        elevation: 0,
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          backgroundColor: AppColors.primary,
          foregroundColor: AppColors.bgDark,
          textStyle: const TextStyle(fontSize: 20, fontWeight: FontWeight.w700),
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          minimumSize: const Size(double.infinity, 52),
        ).copyWith(
            backgroundBuilder: (context, states, child) => DecoratedBox(
                  decoration: BoxDecoration(
                    gradient: states.contains(WidgetState.disabled)
                        ? null
                        : AppColors.brandGradient,
                    borderRadius: BorderRadius.circular(16),
                  ),
                  child: child,
                )),
      ),
      switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected)
              ? AppColors.brandForeground
              : AppColors.textMuted,
        ),
        trackColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected)
              ? AppColors.primary.withValues(alpha: 0.35)
              : AppColors.bgSurface,
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        contentPadding: const EdgeInsets.all(16),
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
      ),
      dividerColor: AppColors.border,
      useMaterial3: true,
    );
  }

  // ── Light ─────────────────────────────────────────────────────────────────
  static ThemeData light() {
    return ThemeData(
      brightness: Brightness.light,
      scaffoldBackgroundColor: AppColors.bgLight,
      primaryColor: AppColors.primaryDark,
      colorScheme: const ColorScheme.light(
        primary: AppColors.primaryDark,
        secondary: AppColors.accent,
        surface: AppColors.bgCardLight,
        error: AppColors.dangerLight,
        onPrimary: AppColors.white,
        onSecondary: AppColors.bgDark,
        onSurface: AppColors.textPrimaryLight,
        onError: AppColors.white,
      ),
      textTheme: const TextTheme(
        displayLarge: TextStyle(
            fontSize: 48,
            fontWeight: FontWeight.w700,
            color: AppColors.textPrimaryLight),
        displayMedium: TextStyle(
            fontSize: 36,
            fontWeight: FontWeight.w700,
            color: AppColors.textPrimaryLight),
        headlineLarge: TextStyle(
            fontSize: 24,
            fontWeight: FontWeight.w600,
            color: AppColors.textPrimaryLight),
        headlineMedium: TextStyle(
            fontSize: 20,
            fontWeight: FontWeight.w600,
            color: AppColors.textPrimaryLight),
        titleLarge: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w600,
            color: AppColors.textPrimaryLight),
        bodyLarge: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w400,
            color: AppColors.textPrimaryLight),
        bodyMedium: TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w400,
            color: AppColors.textSecondaryLight),
        bodySmall: TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w400,
            color: AppColors.textMutedLight),
        labelLarge: TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w600,
            color: AppColors.primaryDark),
      ),
      cardTheme: const CardThemeData(
        color: AppColors.bgCardLight,
        elevation: 0,
        shadowColor: AppColors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.all(Radius.circular(16)),
          side: BorderSide(color: AppColors.borderLight),
        ),
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: AppColors.bgCardLight,
        elevation: 0,
        centerTitle: false,
        titleTextStyle: TextStyle(
          fontSize: 18,
          fontWeight: FontWeight.w600,
          color: AppColors.textPrimaryLight,
        ),
        iconTheme: IconThemeData(color: AppColors.textPrimaryLight),
      ),
      bottomNavigationBarTheme: const BottomNavigationBarThemeData(
        backgroundColor: AppColors.bgCardLight,
        selectedItemColor: AppColors.primaryDark,
        unselectedItemColor: AppColors.textMutedLight,
        type: BottomNavigationBarType.fixed,
        elevation: 0,
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          backgroundColor: AppColors.primaryDark,
          foregroundColor: AppColors.bgDark,
          textStyle: const TextStyle(fontSize: 20, fontWeight: FontWeight.w700),
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          minimumSize: const Size(double.infinity, 52),
        ).copyWith(
            backgroundBuilder: (context, states, child) => DecoratedBox(
                  decoration: BoxDecoration(
                    gradient: states.contains(WidgetState.disabled)
                        ? null
                        : AppColors.brandGradient,
                    borderRadius: BorderRadius.circular(16),
                  ),
                  child: child,
                )),
      ),
      switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected)
              ? AppColors.primaryDark
              : AppColors.textMutedLight,
        ),
        trackColor: WidgetStateProperty.resolveWith(
          (s) => s.contains(WidgetState.selected)
              ? AppColors.primaryDark.withValues(alpha: 0.35)
              : AppColors.bgSurfaceLight,
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        contentPadding: const EdgeInsets.all(16),
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
      ),
      dividerColor: AppColors.borderLight,
      useMaterial3: true,
    );
  }
}

class AppPalette {
  final bool dark;
  const AppPalette(this.dark);
  Color get bgDark => dark ? AppColors.bgDark : AppColors.bgLight;
  Color get bgCard => dark ? AppColors.bgCard : AppColors.bgCardLight;
  Color get bgSurface => dark ? AppColors.bgSurface : AppColors.bgSurfaceLight;
  Color get textPrimary =>
      dark ? AppColors.textPrimary : AppColors.textPrimaryLight;
  Color get textSecondary =>
      dark ? AppColors.textSecondary : AppColors.textSecondaryLight;
  Color get textMuted => dark ? AppColors.textMuted : AppColors.textMutedLight;
  Color get border => dark ? AppColors.border : AppColors.borderLight;
  Color get success => dark ? AppColors.success : AppColors.successLight;
  Color get warning => dark ? AppColors.warning : AppColors.warningLight;
  Color get danger => dark ? AppColors.danger : AppColors.dangerLight;
  Color get primary => dark ? AppColors.brandForeground : AppColors.primaryDark;
}
