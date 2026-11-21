import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../core/bio_provider.dart';
import '../core/config.dart';
import 'theme.dart';

class ConnectPage extends StatefulWidget {
  const ConnectPage({super.key});

  @override
  State<ConnectPage> createState() => _ConnectPageState();
}

class _ConnectPageState extends State<ConnectPage> {
  late final TextEditingController _ctrl;

  @override
  void initState() {
    super.initState();
    _ctrl = TextEditingController(text: AppConfig.chipId);
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    final id = _ctrl.text.trim();
    if (id.isEmpty) return;
    await context.read<BioProvider>().setChipAndConnect(id);
    if (mounted) Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    final bio = context.watch<BioProvider>();

    return Scaffold(
      body: Container(
        decoration: BioTheme.screenGradient(),
        child: SafeArea(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(24, 12, 24, 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                IconButton(
                  onPressed: () => Navigator.pop(context),
                  icon: const Icon(Icons.arrow_back_rounded),
                  color: BioTheme.cream,
                ),
                const SizedBox(height: 12),
                Text(
                  'Kết nối thiết bị',
                  style: Theme.of(context).textTheme.displayMedium?.copyWith(
                        fontSize: 32,
                      ),
                ),
                const SizedBox(height: 8),
                Text(
                  'Nhập chipId khớp firmware (mặc định 800).',
                  style: TextStyle(
                    color: BioTheme.soft.withValues(alpha: 0.85),
                    height: 1.4,
                  ),
                ),
                const SizedBox(height: 28),
                TextField(
                  controller: _ctrl,
                  keyboardType: TextInputType.text,
                  style: const TextStyle(color: BioTheme.cream, fontSize: 18),
                  decoration: InputDecoration(
                    labelText: 'Chip ID',
                    labelStyle: TextStyle(color: BioTheme.moss.withValues(alpha: 0.9)),
                    filled: true,
                    fillColor: BioTheme.bgMid.withValues(alpha: 0.8),
                    border: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: BorderSide(
                        color: BioTheme.leaf.withValues(alpha: 0.35),
                      ),
                    ),
                    enabledBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: BorderSide(
                        color: BioTheme.leaf.withValues(alpha: 0.35),
                      ),
                    ),
                    focusedBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: const BorderSide(color: BioTheme.leaf, width: 1.5),
                    ),
                  ),
                ),
                const SizedBox(height: 20),
                SizedBox(
                  width: double.infinity,
                  height: 48,
                  child: FilledButton(
                    onPressed: bio.connecting ? null : _save,
                    style: FilledButton.styleFrom(
                      backgroundColor: BioTheme.leafDark,
                      foregroundColor: BioTheme.cream,
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12),
                      ),
                    ),
                    child: Text(
                      bio.connecting ? 'Đang kết nối...' : 'Lưu & kết nối',
                      style: const TextStyle(fontWeight: FontWeight.w700),
                    ),
                  ),
                ),
                const SizedBox(height: 16),
                Text(
                  bio.friendlyStatus,
                  style: TextStyle(
                    fontSize: 13,
                    color: BioTheme.soft.withValues(alpha: 0.75),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
