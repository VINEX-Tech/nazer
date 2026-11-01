import 'package:flutter/material.dart';

class BrandLogo extends StatelessWidget {
  const BrandLogo({super.key, this.height = 40});

  final double height;

  @override
  Widget build(BuildContext context) => Image.asset(
        Theme.of(context).brightness == Brightness.dark
            ? 'assets/brand/logo_full_white.png'
            : 'assets/brand/logo_full.png',
        height: height,
        fit: BoxFit.contain,
        semanticLabel: 'NAZER',
      );
}
