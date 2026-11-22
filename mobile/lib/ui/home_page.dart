import 'dart:ui';

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
                            const SizedBox(height: 18),
                            Text(
                              bio.friendlyStatus,
                              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                                    fontSize: 13.5,
                                    color: BioTheme.soft.withValues(alpha: 0.75),
                                  ),
                            ),
                            if (bio.lastUpdateLabel.isNotEmpty) ...[
                              const SizedBox(height: 4),
                              Text(
                                bio.lastUpdateLabel,
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

                    // CO2 hero — số cố định kích thước, tabular digits
                    SliverToBoxAdapter(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(24, 20, 24, 8),
                        child: _Co2Hero(
                          ppm: bio.co2,
                          label: bio.co2Label,
                        ),
                      ),
                    ),

                    // Temp + Humi — cùng kiểu số lớn, đồng đều
                    SliverToBoxAdapter(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(24, 12, 24, 8),
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
                              ),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: _MetricTile(
                                title: 'Độ ẩm',
                                value: bio.humi == null
                                    ? '--'
                                    : bio.humi!.toStringAsFixed(0),
                                unit: '%',
                                accent: BioTheme.leaf,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),

                    SliverToBoxAdapter(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(24, 20, 24, 40),
                        child: SensorChartsSection(bio: bio),
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
}

/// Số cảm biến: cùng font, tabular figures → "550" / "1200" không nhảy to–bé.
class _SensorNumber extends StatelessWidget {
  const _SensorNumber({
    required this.text,
    required this.fontSize,
    this.color = BioTheme.cream,
    this.maxLines = 1,
  });

  final String text;
  final double fontSize;
  final Color color;
  final int maxLines;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      maxLines: maxLines,
      overflow: TextOverflow.visible,
      softWrap: false,
      style: TextStyle(
        fontFamily: 'Georgia',
        fontSize: fontSize,
        height: 1.0,
        fontWeight: FontWeight.w700,
        letterSpacing: 0.5,
        color: color,
        fontFeatures: const [
          FontFeature.tabularFigures(),
          FontFeature.liningFigures(),
        ],
      ),
    );
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
    final text = ppm == null ? '----' : ppm!.toStringAsFixed(0);

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
        const SizedBox(height: 14),
        // Chiều cao cố định — 3 chữ số (550) và 4 chữ số (1200) cùng cỡ nhìn
        SizedBox(
          height: 78,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Flexible(
                child: FittedBox(
                  fit: BoxFit.scaleDown,
                  alignment: Alignment.bottomLeft,
                  child: _SensorNumber(
                    text: text,
                    fontSize: 68,
                    color: BioTheme.cream,
                  ),
                ),
              ),
              const SizedBox(width: 10),
              Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: Text(
                  'ppm',
                  style: TextStyle(
                    fontSize: 20,
                    fontWeight: FontWeight.w600,
                    color: BioTheme.soft.withValues(alpha: 0.85),
                  ),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
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
      padding: const EdgeInsets.fromLTRB(14, 14, 14, 16),
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
          const SizedBox(height: 12),
          SizedBox(
            height: 48,
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                Flexible(
                  child: FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: Alignment.bottomLeft,
                    child: _SensorNumber(
                      text: value,
                      fontSize: 44,
                      color: BioTheme.cream,
                    ),
                  ),
                ),
                const SizedBox(width: 6),
                Padding(
                  padding: const EdgeInsets.only(bottom: 6),
                  child: Text(
                    unit,
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                      color: BioTheme.soft.withValues(alpha: 0.85),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
