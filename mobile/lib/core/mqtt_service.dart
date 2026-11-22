import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

import 'config.dart';

/// Trạng thái kết nối MQTT — BioProvider lắng nghe để sync UI (không đoán từ lần connect()).
enum MqttConnEvent { connected, disconnected, autoReconnected }

class MqttService {
  MqttService({this.clientPrefix = 'aisinhhoc'});

  final String clientPrefix;

  MqttServerClient? _client;
  StreamSubscription<List<MqttReceivedMessage<MqttMessage?>>>? _updatesSub;
  bool _connecting = false;
  bool _isConnected = false;
  bool get isConnected =>
      _isConnected &&
      _client?.connectionStatus?.state == MqttConnectionState.connected;

  String lastError = '';
  String clientId = '';

  final _messageController = StreamController<Map<String, dynamic>>.broadcast();
  Stream<Map<String, dynamic>> get messages => _messageController.stream;

  final _connController = StreamController<MqttConnEvent>.broadcast();
  Stream<MqttConnEvent> get connectionEvents => _connController.stream;

  Future<bool> connect({
    String brokerUrl = AppConfig.brokerUrl,
    bool force = false,
  }) async {
    if (_connecting) return isConnected;
    if (!force && isConnected) {
      resubscribe();
      return true;
    }

    _connecting = true;
    lastError = '';
    // Chỉ tear-down khi force / lần đầu — tránh đánh nhau với autoReconnect
    await _safeDisconnect(notify: false);

    final uri = Uri.parse(brokerUrl);
    final scheme = uri.scheme.isNotEmpty ? uri.scheme : 'wss';
    final host = uri.host.isNotEmpty ? uri.host : 'mqtt.duynguyen.io.vn';
    final port = uri.port != 0 ? uri.port : 443;
    final path = uri.path.isNotEmpty ? uri.path : '/mqtt';
    clientId = '${clientPrefix}_${DateTime.now().millisecondsSinceEpoch}';

    final wsUrl = '$scheme://$host$path';
    if (kDebugMode) print('MQTT: connecting $wsUrl');

    final client = MqttServerClient.withPort(wsUrl, clientId, port);
    _client = client;
    client.useWebSocket = true;
    client.websocketProtocols = MqttClientConstants.protocolsSingleDefault;
    client.logging(on: kDebugMode);
    client.setProtocolV311();
    // Keepalive dài hơn — khớp Cloudflare / mạng điện thoại (tránh cắt sớm)
    client.keepAlivePeriod = 40;
    client.connectTimeoutPeriod = 25000;
    client.autoReconnect = true;
    client.resubscribeOnAutoReconnect = true;

    // Listen TRƯỚC connect — không bỏ lỡ retained messages
    _listen();

    client.onConnected = () {
      _isConnected = true;
      _listen();
      resubscribe();
      _emit(MqttConnEvent.connected);
      if (kDebugMode) print('MQTT: connected $clientId');
    };
    client.onDisconnected = () {
      _isConnected = false;
      _emit(MqttConnEvent.disconnected);
      if (kDebugMode) print('MQTT: disconnected');
    };
    client.onAutoReconnected = () {
      _isConnected = true;
      _listen();
      resubscribe();
      _emit(MqttConnEvent.autoReconnected);
      if (kDebugMode) print('MQTT: auto-reconnected');
    };

    client.connectionMessage =
        MqttConnectMessage().withClientIdentifier(clientId).startClean();

    try {
      await client.connect().timeout(const Duration(seconds: 28));
    } catch (e) {
      lastError = e.toString();
      _isConnected = false;
      _connecting = false;
      await _safeDisconnect(notify: true);
      return false;
    }

    final ok = client.connectionStatus?.state == MqttConnectionState.connected;
    _isConnected = ok;
    _connecting = false;

    if (!ok) {
      lastError = client.connectionStatus?.toString() ?? 'connect failed';
      await _safeDisconnect(notify: true);
      return false;
    }

    await Future<void>.delayed(const Duration(milliseconds: 200));
    resubscribe();
    _emit(MqttConnEvent.connected);
    return true;
  }

  /// Socket còn sống nhưng im telemetry → chỉ sub lại, không disconnect.
  void resubscribe() {
    final client = _client;
    if (client == null) return;
    if (client.connectionStatus?.state != MqttConnectionState.connected) return;
    for (final topic in AppConfig.subscribeTopics) {
      client.subscribe(topic, MqttQos.atLeastOnce);
      if (kDebugMode) print('MQTT: sub $topic');
    }
  }

  void _listen() {
    _updatesSub?.cancel();
    final client = _client;
    if (client == null) return;
    _updatesSub = client.updates?.listen((events) {
      for (final event in events) {
        final rec = event.payload as MqttPublishMessage;
        final topic = event.topic;
        final payload =
            MqttPublishPayload.bytesToStringAsString(rec.payload.message);
        if (kDebugMode) print('MQTT: $topic <= $payload');
        if (!_messageController.isClosed) {
          _messageController.add({'topic': topic, 'payload': payload});
        }
      }
    });
  }

  void _emit(MqttConnEvent e) {
    if (!_connController.isClosed) _connController.add(e);
  }

  Future<void> disconnect() async {
    await _safeDisconnect(notify: true);
  }

  Future<void> _safeDisconnect({bool notify = true}) async {
    _updatesSub?.cancel();
    _updatesSub = null;
    try {
      _client?.disconnect();
    } catch (_) {}
    _client = null;
    _isConnected = false;
    if (notify) _emit(MqttConnEvent.disconnected);
  }

  static double? parseValue(String payload) {
    final raw = payload.trim();
    if (raw.isEmpty) return null;
    if (raw.startsWith('{')) {
      try {
        final map = jsonDecode(raw) as Map<String, dynamic>;
        final v = map['value'];
        if (v is num) return v.toDouble();
        if (v is String) return double.tryParse(v);
      } catch (_) {
        return null;
      }
    }
    return double.tryParse(raw);
  }

  void dispose() {
    _messageController.close();
    _connController.close();
    _safeDisconnect(notify: false);
  }
}
