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
          'Tối đa ${BioProvider.historyMax} mẫu gần nhất',
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
          minHint: 400,
          maxHint: 2000,
        ),
        const SizedBox(height: 16),
        _ChartCard(
          title: 'Nhiệt độ',
          unit: '°C',
          color: BioTheme.warn,
          points: bio.tempHistory.toList(),
          minHint: 15,
          maxHint: 40,
        ),
        const SizedBox(height: 16),
        _ChartCard(
          title: 'Độ ẩm',
          unit: '%',
          color: BioTheme.leaf,
          points: bio.humiHistory.toList(),
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
    required this.minHint,
    required this.maxHint,
  });

  final String title;
  final String unit;
  final Color color;
  final List<SensorPoint> points;
  final double minHint;
  final double maxHint;

  @override
  Widget build(BuildContext context) {
    final hasData = points.length >= 2;
    final latest = points.isEmpty ? null : points.last.value;

    double minY = minHint;
    double maxY = maxHint;
    if (hasData) {
      final vals = points.map((e) => e.value);
      minY = vals.reduce((a, b) => a < b ? a : b);
      maxY = vals.reduce((a, b) => a > b ? a : b);
      final pad = ((maxY - minY).abs() < 1 ? 2.0 : (maxY - minY) * 0.18);
      minY -= pad;
      maxY += pad;
      if (minY == maxY) {
        minY -= 1;
        maxY += 1;
      }
    }

    return Container(
      padding: const EdgeInsets.fromLTRB(16, 14, 16, 12),
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
                latest == null
                    ? '-- $unit'
                    : '${_fmt(latest)} $unit',
                style: TextStyle(
                  fontFamily: 'Georgia',
                  fontSize: 16,
                  fontWeight: FontWeight.w700,
                  color: color,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          SizedBox(
            height: 140,
            child: hasData
                ? LineChart(
                    LineChartData(
                      minY: minY,
                      maxY: maxY,
                      minX: 0,
                      maxX: (points.length - 1).toDouble(),
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
                        bottomTitles: const AxisTitles(
                          sideTitles: SideTitles(showTitles: false),
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
                          getTooltipItems: (spots) => spots
                              .map(
                                (s) => LineTooltipItem(
                                  '${s.y.toStringAsFixed(s.y.abs() >= 100 ? 0 : 1)} $unit',
                                  TextStyle(
                                    color: color,
                                    fontWeight: FontWeight.w600,
                                    fontSize: 12,
                                  ),
                                ),
                              )
                              .toList(),
                        ),
                      ),
                      lineBarsData: [
                        LineChartBarData(
                          spots: [
                            for (var i = 0; i < points.length; i++)
                              FlSpot(i.toDouble(), points[i].value),
                          ],
                          isCurved: true,
                          curveSmoothness: 0.28,
                          color: color,
                          barWidth: 2.4,
                          isStrokeCapRound: true,
                          dotData: const FlDotData(show: false),
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
