import 'package:flutter_test/flutter_test.dart';

import 'package:ai_sinh_hoc/main.dart';

void main() {
  testWidgets('App builds home title', (WidgetTester tester) async {
    await tester.pumpWidget(const AiSinhHocApp());
    await tester.pump(const Duration(milliseconds: 200));
    expect(find.textContaining('SINH'), findsWidgets);
  });
}
