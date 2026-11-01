import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import '../theme/app_theme.dart';

class AppBottomNavigation extends StatelessWidget {
  final int currentIndex;
  const AppBottomNavigation({super.key, required this.currentIndex});

  static final List<String> _routes = [
    '/home',
    '/map',
    '/violations',
    '/vehicle',
    '/settings'
  ];

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: AppColors.of(context).bgCard,
        border: Border(
            top: BorderSide(color: AppColors.of(context).border, width: 1)),
      ),
      child: BottomNavigationBar(
        currentIndex: currentIndex,
        onTap: (i) => context.go(_routes[i]),
        items: const [
          BottomNavigationBarItem(
              icon: Icon(Icons.home_outlined),
              activeIcon: Icon(Icons.home),
              label: 'Home'),
          BottomNavigationBarItem(
              icon: Icon(Icons.map_outlined),
              activeIcon: Icon(Icons.map),
              label: 'Map'),
          BottomNavigationBarItem(
              icon: Icon(Icons.warning_outlined),
              activeIcon: Icon(Icons.warning),
              label: 'Alerts'),
          BottomNavigationBarItem(
              icon: Icon(Icons.two_wheeler), label: 'Vehicle'),
          BottomNavigationBarItem(
              icon: Icon(Icons.settings_outlined), label: 'Settings'),
        ],
      ),
    );
  }
}
