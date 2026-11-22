import 'dart:ui';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../core/bio_provider.dart';
import 'theme.dart';

class SensorChartsSection extends StatelessWidget {
  const SensorChartsSection({super.key, required this.bio});

  final BioProvider bio;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'BIỂU ĐỒ THỜI GIAN',
          style: TextStyle(
            fontSize: 12,
            letterSpacing: 1.4,
            fontWeight: FontWeight.w700,
            color: BioTheme.moss.withValues(alpha: 0.85),
          ),
        ),
        const SizedBox(height: 6),
        Text(
          'Trục dưới = giờ:phút · mẫu ~5s · số góc phải là realtime',
          style: TextStyle(
            fontSize: 12.5,
            color: BioTheme.soft.withValues(alpha: 0.65),
          ),
        ),
        const SizedBox(height: 18),
        _ChartCard(
          title: 'CO₂',
          unit: 'ppm',
          color: BioTheme.co2,
          points: bio.co2History.toList(),
          liveValue: bio.co2,
          minSpan: 80,
          minHint: 350,
          maxHint: 2000,
        ),
        const SizedBox(height: 16),
        _ChartCard(
          title: 'Nhiệt độ',
          unit: '°C',
          color: BioTheme.warn,
          points: bio.tempHistory.toList(),
          liveValue: bio.temp,
          minSpan: 4,
          minHint: 15,
          maxHint: 40,
        ),
        const SizedBox(height: 16),
        _ChartCard(
          title: 'Độ ẩm',
          unit: '%',
          color: BioTheme.leaf,
          points: bio.humiHistory.toList(),
          liveValue: bio.humi,
          minSpan: 10,
          minHint: 20,
          maxHint: 90,
        ),
      ],
    );
  }
}

class _ChartCard extends StatelessWidget {
  const _ChartCard({
    required this.title,
    required this.unit,
    required this.color,
    required this.points,
    required this.liveValue,
    required this.minSpan,
    required this.minHint,
    required this.maxHint,
  });

  final String title;
  final String unit;
  final Color color;
  final List<SensorPoint> points;
  final double? liveValue;
  final double minSpan;
  final double minHint;
  final double maxHint;

  static String _hhmm(DateTime t) {
    final h = t.hour.toString().padLeft(2, '0');
    final m = t.minute.toString().padLeft(2, '0');
    return '$h:$m';
  }

  static String _hhmmss(DateTime t) {
    final h = t.hour.toString().padLeft(2, '0');
    final m = t.minute.toString().padLeft(2, '0');
    final s = t.second.toString().padLeft(2, '0');
    return '$h:$m:$s';
  }

  @override
  Widget build(BuildContext context) {
    final display = liveValue ?? (points.isEmpty ? null : points.last.value);

    final plot = <SensorPoint>[...points];
    if (liveValue != null) {
      if (plot.isEmpty || plot.last.value != liveValue) {
        plot.add(SensorPoint(DateTime.now(), liveValue!));
      }
    }
    if (plot.length == 1) {
      plot.insert(
        0,
        SensorPoint(
          plot.first.at.subtract(const Duration(seconds: 1)),
          plot.first.value,
        ),
      );
    }
    final hasData = plot.length >= 2;

    // Trục X theo thời gian thật (giây epoch) — hiện HH:mm phía dưới
    final t0 = hasData ? plot.first.at : DateTime.now();
    double xOf(DateTime at) =>
        at.difference(t0).inMilliseconds / 1000.0;

    double minY = minHint;
    double maxY = maxHint;
    double minX = 0;
    double maxX = 1;
    if (hasData) {
      final vals = plot.map((e) => e.value);
      var lo = vals.reduce((a, b) => a < b ? a : b);
      var hi = vals.reduce((a, b) => a > b ? a : b);
      if (hi - lo < minSpan) {
        final mid = (lo + hi) / 2;
        lo = mid - minSpan / 2;
        hi = mid + minSpan / 2;
      } else {
        final pad = (hi - lo) * 0.12;
        lo -= pad;
        hi += pad;
      }
      minY = lo;
      maxY = hi;
      minX = xOf(plot.first.at);
      maxX = xOf(plot.last.at);
      if (maxX - minX < 1) maxX = minX + 1;
    }

    // ~4 mốc thời gian trên trục X
    final xSpan = maxX - minX;
    final xInterval = (xSpan / 3).clamp(1.0, 3600.0);

    final chartKey = ValueKey(
      '${title}_${plot.length}_${display?.toStringAsFixed(2) ?? '-'}_'
      '${plot.isEmpty ? 0 : plot.last.at.millisecondsSinceEpoch}',
    );

    return Container(
      padding: const EdgeInsets.fromLTRB(16, 14, 16, 8),
      decoration: BoxDecoration(
        color: BioTheme.bgMid.withValues(alpha: 0.55),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: color.withValues(alpha: 0.28)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(shape: BoxShape.circle, color: color),
              ),
              const SizedBox(width: 8),
              Text(
                title,
                style: TextStyle(
                  fontWeight: FontWeight.w700,
                  fontSize: 14,
                  color: BioTheme.cream.withValues(alpha: 0.95),
                ),
              ),
              const Spacer(),
              Text(
                display == null ? '-- $unit' : '${_fmt(display)} $unit',
                style: TextStyle(
                  fontFamily: 'Georgia',
                  fontSize: 18,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 0.4,
                  color: color,
                  fontFeatures: const [
                    FontFeature.tabularFigures(),
                    FontFeature.liningFigures(),
                  ],
                ),
              ),
            ],
          ),
          if (hasData) ...[
            const SizedBox(height: 4),
            Text(
              '${_hhmmss(plot.first.at)}  →  ${_hhmmss(plot.last.at)}',
              style: TextStyle(
                fontSize: 11,
                color: BioTheme.soft.withValues(alpha: 0.5),
              ),
            ),
          ],
          const SizedBox(height: 10),
          SizedBox(
            height: 156,
            child: hasData
                ? LineChart(
                    key: chartKey,
                    LineChartData(
                      minY: minY,
                      maxY: maxY,
                      minX: minX,
                      maxX: maxX,
                      clipData: const FlClipData.all(),
                      gridData: FlGridData(
                        show: true,
                        drawVerticalLine: false,
                        horizontalInterval: (maxY - minY) / 3,
                        getDrawingHorizontalLine: (v) => FlLine(
                          color: BioTheme.soft.withValues(alpha: 0.12),
                          strokeWidth: 1,
                        ),
                      ),
                      titlesData: FlTitlesData(
                        show: true,
                        topTitles: const AxisTitles(
                          sideTitles: SideTitles(showTitles: false),
                        ),
                        rightTitles: const AxisTitles(
                          sideTitles: SideTitles(showTitles: false),
                        ),
                        bottomTitles: AxisTitles(
                          sideTitles: SideTitles(
                            showTitles: true,
                            reservedSize: 22,
                            interval: xInterval,
                            getTitlesWidget: (v, meta) {
                              // Bỏ nhãn sát mép để khỏi chồng
                              if ((v - meta.min).abs() < 0.01 ||
                                  (v - meta.max).abs() < 0.01) {
                                return const SizedBox.shrink();
                              }
                              final t = t0.add(
                                Duration(milliseconds: (v * 1000).round()),
                              );
                              return Padding(
                                padding: const EdgeInsets.only(top: 4),
                                child: Text(
                                  _hhmm(t),
                                  style: TextStyle(
                                    fontSize: 10,
                                    color: BioTheme.soft.withValues(alpha: 0.55),
                                  ),
                                ),
                              );
                            },
                          ),
                        ),
                        leftTitles: AxisTitles(
                          sideTitles: SideTitles(
                            showTitles: true,
                            reservedSize: 34,
                            interval: (maxY - minY) / 2,
                            getTitlesWidget: (v, meta) => Text(
                              v.toStringAsFixed(v.abs() >= 100 ? 0 : 1),
                              style: TextStyle(
                                fontSize: 10,
                                color: BioTheme.soft.withValues(alpha: 0.55),
                              ),
                            ),
                          ),
                        ),
                      ),
                      borderData: FlBorderData(show: false),
                      lineTouchData: LineTouchData(
                        enabled: true,
                        touchTooltipData: LineTouchTooltipData(
                          getTooltipColor: (_) => BioTheme.bgDeep,
                          getTooltipItems: (spots) => spots.map((s) {
                            final t = t0.add(
                              Duration(milliseconds: (s.x * 1000).round()),
                            );
                            return LineTooltipItem(
                              '${_hhmmss(t)}\n'
                              '${s.y.toStringAsFixed(s.y.abs() >= 100 ? 0 : 1)} $unit',
                              TextStyle(
                                color: color,
                                fontWeight: FontWeight.w600,
                                fontSize: 12,
                              ),
                            );
                          }).toList(),
                        ),
                      ),
                      lineBarsData: [
                        LineChartBarData(
                          spots: [
                            for (final p in plot) FlSpot(xOf(p.at), p.value),
                          ],
                          isCurved: true,
                          curveSmoothness: 0.2,
                          color: color,
                          barWidth: 2.4,
                          isStrokeCapRound: true,
                          dotData: FlDotData(
                            show: plot.length <= 8,
                            getDotPainter: (spot, percent, bar, index) =>
                                FlDotCirclePainter(
                              radius: 2.5,
                              color: color,
                              strokeWidth: 0,
                            ),
                          ),
                          belowBarData: BarAreaData(
                            show: true,
                            gradient: LinearGradient(
                              begin: Alignment.topCenter,
                              end: Alignment.bottomCenter,
                              colors: [
                                color.withValues(alpha: 0.28),
                                color.withValues(alpha: 0.02),
                              ],
                            ),
                          ),
                        ),
                      ],
                    ),
                  )
                : Center(
                    child: Text(
                      'Đang thu thập dữ liệu…',
                      style: TextStyle(
                        color: BioTheme.soft.withValues(alpha: 0.55),
                        fontSize: 13,
                      ),
                    ),
                  ),
          ),
        ],
      ),
    );
  }

  static String _fmt(double v) {
    if (v.abs() >= 100) return v.toStringAsFixed(0);
    if (v == v.roundToDouble()) return v.toStringAsFixed(0);
    return v.toStringAsFixed(1);
  }
}
