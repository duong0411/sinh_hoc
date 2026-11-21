import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'config.dart';
import 'mqtt_service.dart';

class BioProvider extends ChangeNotifier {
  BioProvider() {
    _mqtt = MqttService();
    _sub = _mqtt.messages.listen(_onMessage);
    _restore();
  }

  late final MqttService _mqtt;
  StreamSubscription<Map<String, dynamic>>? _sub;

  double? temp;
  double? humi;
  double? co2;
  DateTime? lastUpdate;
  bool deviceOnline = false;
  bool mqttConnected = false;
  bool connecting = false;
  String status = 'Chưa kết nối';
  String? error;

  bool get hasTelemetry => temp != null || humi != null || co2 != null;

  String get friendlyStatus {
    if (connecting) return 'Đang kết nối MQTT...';
    if (!mqttConnected) return error ?? 'Mất kết nối MQTT';
    if (!deviceOnline && !hasTelemetry) {
      return 'MQTT OK — chờ ESP32 (chip ${AppConfig.chipId})';
    }
    if (!deviceOnline) return 'ESP32 offline — hiển thị dữ liệu gần nhất';
    return 'Trực tiếp · chip ${AppConfig.chipId}';
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

  void _onMessage(Map<String, dynamic> msg) {
    final topic = msg['topic'] as String? ?? '';
    final payload = (msg['payload'] as String? ?? '').trim();

    if (topic == AppConfig.topicOnline) {
      deviceOnline = payload.toLowerCase() == 'online';
      notifyListeners();
      return;
    }

    final value = MqttService.parseValue(payload);
    if (value == null) return;

    if (topic == AppConfig.topicTemp) {
      temp = value;
    } else if (topic == AppConfig.topicHumi) {
      humi = value;
    } else if (topic == AppConfig.topicCo2) {
      co2 = value;
    } else {
      return;
    }

    lastUpdate = DateTime.now();
    deviceOnline = true;
    notifyListeners();
  }

  /// Đánh giá nhanh chất lượng không khí theo eCO2 (ppm)
  String get co2Label {
    final v = co2;
    if (v == null) return 'Chờ dữ liệu';
    if (v < 800) return 'Tốt';
    if (v < 1200) return 'Trung bình';
    if (v < 2000) return 'Cao — nên thông gió';
    return 'Rất cao';
  }

  @override
  void dispose() {
    _sub?.cancel();
    _mqtt.dispose();
    super.dispose();
  }
}
