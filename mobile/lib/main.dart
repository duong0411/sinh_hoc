import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import 'core/bio_provider.dart';
import 'ui/connect_page.dart';
import 'ui/home_page.dart';
import 'ui/theme.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setSystemUIOverlayStyle(
    const SystemUiOverlayStyle(
      statusBarColor: Colors.transparent,
      statusBarIconBrightness: Brightness.light,
    ),
  );
  runApp(const AiSinhHocApp());
}

class AiSinhHocApp extends StatelessWidget {
  const AiSinhHocApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => BioProvider(),
      child: MaterialApp(
        title: 'AI Sinh Học',
        debugShowCheckedModeBanner: false,
        theme: BioTheme.theme(),
        home: const HomePage(),
        routes: {
          '/connect': (_) => const ConnectPage(),
        },
      ),
    );
  }
}
