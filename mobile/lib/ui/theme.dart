import 'package:flutter/material.dart';

class BioTheme {
  static const Color bgDeep = Color(0xFF06140F);
  static const Color bgMid = Color(0xFF0C241A);
  static const Color leaf = Color(0xFF52B788);
  static const Color leafDark = Color(0xFF2D6A4F);
  static const Color moss = Color(0xFF95D5B2);
  static const Color cream = Color(0xFFE8F5E9);
  static const Color soft = Color(0xFFB7C4BC);
  static const Color warn = Color(0xFFE9C46A);
  static const Color danger = Color(0xFFE76F51);
  static const Color co2 = Color(0xFF4CC9F0);

  static ThemeData theme() {
    final base = ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      scaffoldBackgroundColor: bgDeep,
      colorScheme: const ColorScheme.dark(
        primary: leaf,
        secondary: co2,
        surface: bgMid,
        onPrimary: Colors.black,
        onSurface: cream,
      ),
      fontFamily: 'Segoe UI',
    );

    return base.copyWith(
      textTheme: base.textTheme.copyWith(
        displayMedium: const TextStyle(
          fontFamily: 'Georgia',
          fontWeight: FontWeight.w700,
          letterSpacing: -0.5,
          color: cream,
          height: 1.05,
        ),
        titleLarge: const TextStyle(
          fontWeight: FontWeight.w700,
          color: cream,
        ),
        bodyLarge: TextStyle(color: cream.withValues(alpha: 0.92)),
        bodyMedium: TextStyle(color: soft.withValues(alpha: 0.9)),
      ),
    );
  }

  static BoxDecoration screenGradient() => const BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            Color(0xFF071A12),
            Color(0xFF0A2A1C),
            Color(0xFF06140F),
          ],
        ),
      );
}
