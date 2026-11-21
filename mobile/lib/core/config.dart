/// MQTT AloT — khớp firmware chipId 800
class AppConfig {
  static const String brokerUrl = 'wss://mqtt.duynguyen.io.vn/mqtt';
  static const String defaultChipId = '800';
  static const String deviceName = 'AI Sinh Học';

  static String chipId = defaultChipId;

  static void setChipId(String id) {
    final cleaned = id.trim();
    chipId = cleaned.isEmpty ? defaultChipId : cleaned;
  }

  static String get topicOnline => 'tele/$chipId/status';
  static String get topicTemp => 'tele/${chipId}_temp/status';
  static String get topicHumi => 'tele/${chipId}_humi/status';
  static String get topicCo2 => 'tele/${chipId}_co2/status';

  static List<String> get subscribeTopics => [
        topicOnline,
        topicTemp,
        topicHumi,
        topicCo2,
      ];
}
