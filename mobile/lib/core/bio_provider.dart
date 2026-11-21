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

  static const int historyMax = 60;
  static const Duration liveWindow = Duration(seconds: 20);

  late final MqttService _mqtt;
  StreamSubscription<Map<String, dynamic>>? _sub;
  Timer? _staleTimer;

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

  bool get hasTelemetry => temp != null || humi != null || co2 != null;

  /// LIVE thật sự: ESP báo online HOẶC vừa có telemetry < 20s
  bool get isLive {
    if (!mqttConnected) return false;
    if (deviceOnline) return true;
    if (lastUpdate == null) return false;
    return DateTime.now().difference(lastUpdate!) <= liveWindow;
  }

  bool get isStaleData {
    if (!hasTelemetry || lastUpdate == null) return false;
    return DateTime.now().difference(lastUpdate!) > liveWindow;
  }

  String get friendlyStatus {
    if (connecting) return 'Đang kết nối MQTT...';
    if (!mqttConnected) return error ?? 'Mất kết nối MQTT';
    if (isLive) return 'Trực tiếp · chip ${AppConfig.chipId}';
    if (hasTelemetry) {
      return 'ESP32 offline — đang hiện dữ liệu cũ (retain)';
    }
    return 'MQTT OK — chờ ESP32 (chip ${AppConfig.chipId})';
  }

  /// Nhãn thời gian cập nhật — tick mỗi 1s để cảm giác realtime
  String get lastUpdateLabel {
    if (lastUpdate == null) return '';
    final s = DateTime.now().difference(lastUpdate!).inSeconds;
    if (s <= 1) return 'Vừa cập nhật';
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
    temp = null;
    humi = null;
    co2 = null;
    deviceOnline = false;
    lastUpdate = null;
    tempHistory.clear();
    humiHistory.clear();
    co2History.clear();
    notifyListeners();
    await connect(force: true);
  }

  Future<void> connect({bool force = false}) async {
    connecting = true;
    error = null;
    status = 'Đang kết nối...';
    notifyListeners();

    final ok = await _mqtt.connect(force: force);
    mqttConnected = ok;
    connecting = false;
    if (!ok) {
      error = _mqtt.lastError.isEmpty ? 'Không kết nối được broker' : _mqtt.lastError;
      status = 'Lỗi MQTT';
    } else {
      status = 'Đã kết nối';
    }
    notifyListeners();
  }

  Future<void> reconnect() => connect(force: true);

  void _checkStale() {
    if (hasTelemetry || mqttConnected) {
      notifyListeners();
    }
    // Không thấy telemetry mới lâu → reconnect MQTT app
    if (mqttConnected &&
        lastUpdate != null &&
        DateTime.now().difference(lastUpdate!) > const Duration(seconds: 45)) {
      if (kDebugMode) print('BioProvider: stale telemetry → reconnect');
      reconnect();
    }
  }

  void _pushHistory(ListQueue<SensorPoint> q, double value) {
    // Đồng bộ timeline: luôn ghi mẫu mới (kể cả trùng giá trị) để chart chạy realtime
    q.addLast(SensorPoint(DateTime.now(), value));
    while (q.length > historyMax) {
      q.removeFirst();
    }
  }

  void _onMessage(Map<String, dynamic> msg) {
    final topic = msg['topic'] as String? ?? '';
    final payload = (msg['payload'] as String? ?? '').trim();

    if (topic == AppConfig.topicOnline) {
      // Chỉ tin topic online/offline — không đoán từ telemetry retain
      deviceOnline = payload.toLowerCase() == 'online';
      notifyListeners();
      return;
    }

    final value = MqttService.parseValue(payload);
    if (value == null) return;

    if (topic == AppConfig.topicTemp) {
      temp = value;
      _pushHistory(tempHistory, value);
    } else if (topic == AppConfig.topicHumi) {
      humi = value;
      _pushHistory(humiHistory, value);
    } else if (topic == AppConfig.topicCo2) {
      co2 = value;
      _pushHistory(co2History, value);
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
