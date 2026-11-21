import 'package:flutter/material.dart';
import 'package:flutter_animate/flutter_animate.dart';
import 'package:provider/provider.dart';

import '../core/bio_provider.dart';
import '../core/config.dart';
import 'sensor_charts.dart';
import 'theme.dart';

class HomePage extends StatelessWidget {
  const HomePage({super.key});

  @override
  Widget build(BuildContext context) {
    final bio = context.watch<BioProvider>();
    final size = MediaQuery.sizeOf(context);
    final live = bio.isLive;

    return Scaffold(
      body: Container(
        width: double.infinity,
        height: double.infinity,
        decoration: BioTheme.screenGradient(),
        child: Stack(
          children: [
            Positioned(
              top: -size.width * 0.28,
              right: -size.width * 0.18,
              child: _orb(size.width * 0.8, BioTheme.leaf.withValues(alpha: 0.14))
                  .animate(onPlay: (a) => a.repeat(reverse: true))
                  .scale(
                    begin: const Offset(0.94, 0.94),
                    end: const Offset(1.05, 1.05),
                    duration: 8.seconds,
                  ),
            ),
            Positioned(
              bottom: size.height * 0.12,
              left: -60,
              child: _orb(200, BioTheme.co2.withValues(alpha: 0.1))
                  .animate(onPlay: (a) => a.repeat(reverse: true))
                  .fade(begin: 0.35, end: 0.8, duration: 6.seconds),
            ),
            SafeArea(
              child: RefreshIndicator(
                color: BioTheme.leaf,
                backgroundColor: BioTheme.bgMid,
                onRefresh: bio.reconnect,
                child: CustomScrollView(
                  physics: const AlwaysScrollableScrollPhysics(
                    parent: BouncingScrollPhysics(),
                  ),
                  slivers: [
                    SliverToBoxAdapter(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(24, 10, 24, 0),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            _TopBar(
                              live: live,
                              chipId: AppConfig.chipId,
                              onChangeDevice: () =>
                                  Navigator.of(context).pushNamed('/connect'),
                            ),
                            SizedBox(height: size.height * 0.03),
                            Text(
                              'AI SINH HỌC',
                              style: Theme.of(context)
                                  .textTheme
                                  .displayMedium
                                  ?.copyWith(
                                    fontSize: size.width > 390 ? 44 : 36,
                                  ),
                            )
                                .animate()
                                .fadeIn(duration: 450.ms)
                                .slideY(begin: 0.08, curve: Curves.easeOutCubic),
                            const SizedBox(height: 8),
                            Text(
                              'Nhiệt độ · Độ ẩm · Nồng độ CO₂',
                              style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                                    color: BioTheme.moss.withValues(alpha: 0.95),
                                    fontSize: 15,
                                    fontWeight: FontWeight.w500,
                                  ),
                            ).animate().fadeIn(delay: 60.ms),
                            const SizedBox(height: 12),
                            Text(
                              bio.friendlyStatus,
                              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                                    fontSize: 13.5,
                                    color: BioTheme.soft.withValues(alpha: 0.75),
                                  ),
                            ),
                            if (bio.lastUpdate != null) ...[
                              const SizedBox(height: 4),
                              Text(
                                'Cập nhật ${_fmtTime(bio.lastUpdate!)}',
                                style: TextStyle(
                                  fontSize: 12,
                                  color: BioTheme.soft.withValues(alpha: 0.55),
                                ),
                              ),
                            ],
                          ],
                        ),
                      ),
                    ),

                    // CO2 hero
                    SliverToBoxAdapter(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(24, 28, 24, 8),
                        child: _Co2Hero(
                          ppm: bio.co2,
                          label: bio.co2Label,
                        ).animate().fadeIn(delay: 80.ms).slideY(begin: 0.06),
                      ),
                    ),

                    // Temp + Humi
                    SliverToBoxAdapter(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(24, 18, 24, 8),
                        child: Row(
                          children: [
                            Expanded(
                              child: _MetricTile(
                                title: 'Nhiệt độ',
                                value: bio.temp == null
                                    ? '--.-'
                                    : bio.temp!.toStringAsFixed(1),
                                unit: '°C',
                                accent: BioTheme.warn,
                              ).animate().fadeIn(delay: 120.ms),
                            ),
                            const SizedBox(width: 14),
                            Expanded(
                              child: _MetricTile(
                                title: 'Độ ẩm',
                                value: bio.humi == null
                                    ? '--'
                                    : bio.humi!.toStringAsFixed(0),
                                unit: '%',
                                accent: BioTheme.leaf,
                              ).animate().fadeIn(delay: 160.ms),
                            ),
                          ],
                        ),
                      ),
                    ),

                    // Charts
                    SliverToBoxAdapter(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(24, 20, 24, 40),
                        child: SensorChartsSection(bio: bio)
                            .animate()
                            .fadeIn(delay: 200.ms)
                            .slideY(begin: 0.04),
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

  static Widget _orb(double size, Color color) {
    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: color,
      ),
    );
  }

  static String _fmtTime(DateTime t) {
    final h = t.hour.toString().padLeft(2, '0');
    final m = t.minute.toString().padLeft(2, '0');
    final s = t.second.toString().padLeft(2, '0');
    return '$h:$m:$s';
  }
}

class _TopBar extends StatelessWidget {
  const _TopBar({
    required this.live,
    required this.chipId,
    required this.onChangeDevice,
  });

  final bool live;
  final String chipId;
  final VoidCallback onChangeDevice;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
          decoration: BoxDecoration(
            color: BioTheme.bgMid.withValues(alpha: 0.7),
            borderRadius: BorderRadius.circular(20),
            border: Border.all(
              color: live
                  ? BioTheme.leaf.withValues(alpha: 0.5)
                  : BioTheme.soft.withValues(alpha: 0.25),
            ),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: live ? BioTheme.leaf : BioTheme.soft,
                ),
              ),
              const SizedBox(width: 8),
              Text(
                live ? 'LIVE' : 'IDLE',
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 0.8,
                  color: live ? BioTheme.leaf : BioTheme.soft,
                ),
              ),
              const SizedBox(width: 8),
              Text(
                chipId,
                style: TextStyle(
                  fontSize: 12,
                  color: BioTheme.cream.withValues(alpha: 0.7),
                ),
              ),
            ],
          ),
        ),
        const Spacer(),
        IconButton(
          onPressed: onChangeDevice,
          icon: const Icon(Icons.settings_input_antenna_rounded),
          color: BioTheme.moss,
          tooltip: 'Đổi chip / kết nối',
        ),
      ],
    );
  }
}

class _Co2Hero extends StatelessWidget {
  const _Co2Hero({required this.ppm, required this.label});

  final double? ppm;
  final String label;

  Color get _accent {
    final v = ppm;
    if (v == null) return BioTheme.soft;
    if (v < 800) return BioTheme.leaf;
    if (v < 1200) return BioTheme.warn;
    return BioTheme.danger;
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'NỒNG ĐỘ CO₂',
          style: TextStyle(
            fontSize: 12,
            letterSpacing: 1.4,
            fontWeight: FontWeight.w600,
            color: BioTheme.co2.withValues(alpha: 0.9),
          ),
        ),
        const SizedBox(height: 10),
        Row(
          crossAxisAlignment: CrossAxisAlignment.end,
          children: [
            Text(
              ppm == null ? '----' : ppm!.toStringAsFixed(0),
              style: const TextStyle(
                fontFamily: 'Georgia',
                fontSize: 72,
                height: 0.95,
                fontWeight: FontWeight.w700,
                color: BioTheme.cream,
              ),
            ),
            const SizedBox(width: 8),
            Padding(
              padding: const EdgeInsets.only(bottom: 12),
              child: Text(
                'ppm',
                style: TextStyle(
                  fontSize: 18,
                  color: BioTheme.soft.withValues(alpha: 0.8),
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        Text(
          label,
          style: TextStyle(
            fontSize: 15,
            fontWeight: FontWeight.w600,
            color: _accent,
          ),
        ),
        const SizedBox(height: 16),
        ClipRRect(
          borderRadius: BorderRadius.circular(4),
          child: LinearProgressIndicator(
            value: ppm == null ? 0 : (ppm! / 2500).clamp(0.0, 1.0),
            minHeight: 6,
            backgroundColor: BioTheme.bgMid,
            color: _accent,
          ),
        ),
      ],
    );
  }
}

class _MetricTile extends StatelessWidget {
  const _MetricTile({
    required this.title,
    required this.value,
    required this.unit,
    required this.accent,
  });

  final String title;
  final String value;
  final String unit;
  final Color accent;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 18),
      decoration: BoxDecoration(
        border: Border(
          left: BorderSide(color: accent.withValues(alpha: 0.85), width: 3),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title.toUpperCase(),
            style: TextStyle(
              fontSize: 11,
              letterSpacing: 1.2,
              fontWeight: FontWeight.w600,
              color: accent.withValues(alpha: 0.9),
            ),
          ),
          const SizedBox(height: 10),
          Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Text(
                value,
                style: const TextStyle(
                  fontFamily: 'Georgia',
                  fontSize: 36,
                  height: 1,
                  fontWeight: FontWeight.w700,
                  color: BioTheme.cream,
                ),
              ),
              const SizedBox(width: 4),
              Padding(
                padding: const EdgeInsets.only(bottom: 4),
                child: Text(
                  unit,
                  style: TextStyle(
                    fontSize: 14,
                    color: BioTheme.soft.withValues(alpha: 0.8),
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
