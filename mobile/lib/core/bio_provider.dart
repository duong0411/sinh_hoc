import 'dart:async';
import 'dart:collection';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'config.dart';
import 'mqtt_service.dart';

class SensorPoint {
  const SensorPoint(this.at, this.value);
  final DateTime at;
  final double value;
}

class BioProvider extends ChangeNotifier {
  BioProvider() {
    _mqtt = MqttService();
    _sub = _mqtt.messages.listen(_onMessage);
    _restore();
    _staleTimer = Timer.periodic(const Duration(seconds: 1), (_) => _checkStale());
  }

  static const int historyMax = 90;
  /// Mẫu biểu đồ denser để thấy thời gian rõ hơn
  static const Duration chartSampleEvery = Duration(seconds: 5);
  /// LIVE nếu còn nhận số trong cửa sổ này (ESP pub ~1.5s)
  static const Duration liveWindow = Duration(seconds: 18);
  /// Chỉ reconnect khi im quá lâu — tránh cắt realtime mỗi ~20s
  static const Duration forceReconnectAfter = Duration(seconds: 55);

  late final MqttService _mqtt;
  StreamSubscription<Map<String, dynamic>>? _sub;
  Timer? _staleTimer;
  bool _reconnectLock = false;
  DateTime? _lastReconnectAt;

  double? temp;
  double? humi;
  double? co2;
  DateTime? lastUpdate;
  bool deviceOnline = false;
  bool mqttConnected = false;
  bool connecting = false;
  String status = 'Chưa kết nối';
  String? error;

  final ListQueue<SensorPoint> tempHistory = ListQueue();
  final ListQueue<SensorPoint> humiHistory = ListQueue();
  final ListQueue<SensorPoint> co2History = ListQueue();

  DateTime? _lastChartTemp;
  DateTime? _lastChartHumi;
  DateTime? _lastChartCo2;

  bool get hasTelemetry => temp != null || humi != null || co2 != null;

  /// LIVE chỉ khi vừa nhận telemetry thật (khớp OLED), không chỉ vì retain/online
  bool get isLive {
    if (!mqttConnected || lastUpdate == null) return false;
    return DateTime.now().difference(lastUpdate!) <= liveWindow;
  }

  bool get isStaleData {
    if (!hasTelemetry || lastUpdate == null) return true;
    return DateTime.now().difference(lastUpdate!) > liveWindow;
  }

  String get friendlyStatus {
    if (connecting) return 'Đang kết nối MQTT...';
    if (!mqttConnected) return error ?? 'Mất kết nối MQTT';
    if (isLive) return 'Trực tiếp · chip ${AppConfig.chipId}';
    if (deviceOnline) return 'ESP online — đang chờ số đo mới';
    if (hasTelemetry) {
      return 'ESP32 offline — đang hiện dữ liệu cũ (retain)';
    }
    return 'MQTT OK — chờ ESP32 (chip ${AppConfig.chipId})';
  }

  String get lastUpdateLabel {
    if (lastUpdate == null) return '';
    final s = DateTime.now().difference(lastUpdate!).inSeconds;
    // ESP pub ~1.5s — coi ≤4s là “vừa cập nhật” để UI không nhảy “2s…3s…”
    if (s <= 4) return 'Vừa cập nhật';
    if (s < 60) return 'Cập nhật $s giây trước';
    final m = s ~/ 60;
    if (m < 60) return 'Cập nhật $m phút trước';
    return 'Cập nhật lúc ${_fmtClock(lastUpdate!)}';
  }

  static String _fmtClock(DateTime t) {
    final h = t.hour.toString().padLeft(2, '0');
    final m = t.minute.toString().padLeft(2, '0');
    final s = t.second.toString().padLeft(2, '0');
    return '$h:$m:$s';
  }

  Future<void> _restore() async {
    final prefs = await SharedPreferences.getInstance();
    final saved = prefs.getString('chipId');
    if (saved != null && saved.isNotEmpty) {
      AppConfig.setChipId(saved);
    }
    await connect();
  }

  Future<void> setChipAndConnect(String chipId) async {
    AppConfig.setChipId(chipId);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('chipId', AppConfig.chipId);
    _clearTelemetry();
    notifyListeners();
    await connect(force: true, clearFreshness: true);
  }

  void _clearTelemetry() {
    temp = null;
    humi = null;
    co2 = null;
    deviceOnline = false;
    lastUpdate = null;
    tempHistory.clear();
    humiHistory.clear();
    co2History.clear();
    _lastChartTemp = null;
    _lastChartHumi = null;
    _lastChartCo2 = null;
  }

  Future<void> connect({
    bool force = false,
    bool clearFreshness = false,
  }) async {
    if (_reconnectLock) return;
    _reconnectLock = true;
    connecting = true;
    error = null;
    status = 'Đang kết nối...';
    notifyListeners();

    final ok = await _mqtt.connect(force: force);
    mqttConnected = ok;
    connecting = false;
    _reconnectLock = false;
    _lastReconnectAt = DateTime.now();

    if (!ok) {
      error = _mqtt.lastError.isEmpty ? 'Không kết nối được broker' : _mqtt.lastError;
      status = 'Lỗi MQTT';
    } else {
      status = 'Đã kết nối';
      // Chỉ xóa freshness khi đổi chip / pull-to-refresh chủ động
      // Soft reconnect giữ lastUpdate → UI không nhảy “10 giây trước”
      if (clearFreshness) lastUpdate = null;
    }
    notifyListeners();
  }

  Future<void> reconnect() => connect(force: true, clearFreshness: true);

  void _checkStale() {
    if (hasTelemetry || mqttConnected) {
      notifyListeners();
    }
    if (!mqttConnected || lastUpdate == null || _reconnectLock) return;

    final silent = DateTime.now().difference(lastUpdate!);
    if (silent <= forceReconnectAfter) return;

    // Tránh spam reconnect
    if (_lastReconnectAt != null &&
        DateTime.now().difference(_lastReconnectAt!) < const Duration(seconds: 15)) {
      return;
    }

    if (kDebugMode) {
      print('BioProvider: silent ${silent.inSeconds}s → soft reconnect');
    }
    // Soft: giữ số đo + lastUpdate; chỉ nối lại socket
    connect(force: true, clearFreshness: false);
  }

  void _pushHistorySparse(
    ListQueue<SensorPoint> q,
    double value,
    DateTime? lastAt,
    void Function(DateTime) setLast,
  ) {
    final now = DateTime.now();
    if (lastAt != null && now.difference(lastAt) < chartSampleEvery) {
      // Cập nhật điểm cuối cho khớp số live, không thêm mẫu realtime
      if (q.isNotEmpty) {
        q.removeLast();
        q.addLast(SensorPoint(now, value));
      }
      return;
    }
    setLast(now);
    q.addLast(SensorPoint(now, value));
    while (q.length > historyMax) {
      q.removeFirst();
    }
  }

  void _onMessage(Map<String, dynamic> msg) {
    final topic = msg['topic'] as String? ?? '';
    final payload = (msg['payload'] as String? ?? '').trim();

    if (topic == AppConfig.topicOnline) {
      final online = payload.toLowerCase() == 'online';
      deviceOnline = online;
      notifyListeners();
      return;
    }

    final value = MqttService.parseValue(payload);
    if (value == null) return;

    // Luôn cập nhật số đo realtime (ưu tiên khớp OLED)
    if (topic == AppConfig.topicTemp) {
      temp = value;
      _pushHistorySparse(
        tempHistory,
        value,
        _lastChartTemp,
        (t) => _lastChartTemp = t,
      );
    } else if (topic == AppConfig.topicHumi) {
      humi = value;
      _pushHistorySparse(
        humiHistory,
        value,
        _lastChartHumi,
        (t) => _lastChartHumi = t,
      );
    } else if (topic == AppConfig.topicCo2) {
      co2 = value;
      _pushHistorySparse(
        co2History,
        value,
        _lastChartCo2,
        (t) => _lastChartCo2 = t,
      );
    } else {
      return;
    }

    lastUpdate = DateTime.now();
    notifyListeners();
  }

  String get co2Label {
    final v = co2;
    if (v == null) return 'Chờ dữ liệu';
    if (isStaleData) return 'Dữ liệu cũ';
    if (v < 800) return 'Tốt';
    if (v < 1200) return 'Trung bình';
    if (v < 2000) return 'Cao — nên thông gió';
    return 'Rất cao';
  }

  @override
  void dispose() {
    _staleTimer?.cancel();
    _sub?.cancel();
    _mqtt.dispose();
    super.dispose();
  }
}
