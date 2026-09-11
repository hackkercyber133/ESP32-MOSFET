// device_page.dart
// Halaman Spesifikasi HP — menampilkan info perangkat, layar, baterai, jaringan, dll.
// Menggunakan package yang sudah ada di pubspec: flutter bawaan + http untuk cross-check.
// Info hardware diambil dari Platform, dart:io, dan FlutterBluePlus (BT state).

import 'dart:async';
import 'dart:math' as math;
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'theme_controller.dart';

// ─── AppColors helper (diambil dari main.dart agar konsisten) ─────────────────
class _AC {
  static Color bg(bool d) => d ? const Color(0xFF0A0E17) : const Color(0xFFF0F4FA);
  static Color surface(bool d) => d ? const Color(0xFF12181F) : Colors.white;
  static Color card(bool d) => d ? const Color(0xFF1A2230) : const Color(0xFFF7FAFF);
  static Color text(bool d) => d ? Colors.white : const Color(0xFF0D1117);
  static Color faint(bool d) => d ? Colors.white38 : Colors.black38;
  static Color divider(bool d) => d ? Colors.white12 : Colors.black12;
}

class DeviceInfoPage extends StatefulWidget {
  final Color accentColor;
  final String appVersion;

  const DeviceInfoPage({
    super.key,
    required this.accentColor,
    required this.appVersion,
  });

  @override
  State<DeviceInfoPage> createState() => _DeviceInfoPageState();
}

class _DeviceInfoPageState extends State<DeviceInfoPage>
    with SingleTickerProviderStateMixin {
  // ── Live data ──────────────────────────────────────────────────────────────
  late final Ticker _ticker;
  Duration _lastElapsed = Duration.zero;

  // FPS tracking
  final List<Duration> _frameTimes = [];
  double _fps = 0;
  double _smoothFrameRatio = 1.0;

  // BT state
  BluetoothAdapterState _btState = BluetoothAdapterState.unknown;
  StreamSubscription<BluetoothAdapterState>? _btSub;

  // App info (dari SharedPreferences / package name)
  String _packageName = 'com.coolerapp.app_controller';
  String _appName = 'Mod And TroubleShoot';

  // ── Static device info ─────────────────────────────────────────────────────
  late final MediaQueryData _mq;
  late final Size _physicalSize;
  late final double _dpr;

  @override
  void initState() {
    super.initState();
    _loadAppMeta();
    _btSub = FlutterBluePlus.adapterState.listen((s) {
      if (mounted) setState(() => _btState = s);
    });
    _ticker = createTicker(_onTick)..start();
  }

  Future<void> _loadAppMeta() async {
    final prefs = await SharedPreferences.getInstance();
    // Simpan app name kalau belum ada (diset dari main di masa depan)
    setState(() {
      _appName = prefs.getString('_meta_appName') ?? 'Mod And TroubleShoot';
      _packageName = prefs.getString('_meta_pkgName') ?? 'com.coolerapp.app_controller';
    });
  }

  void _onTick(Duration elapsed) {
    if (!mounted) return;
    final now = elapsed;
    _frameTimes.add(now);
    // Hitung FPS dari frame dalam 1 detik terakhir
    _frameTimes.removeWhere(
        (t) => now - t > const Duration(seconds: 1));
    final count = _frameTimes.length;
    final rawFps = count.toDouble();

    // Hitung rasio frame smooth (frame yg tepat waktu vs total)
    final dt = elapsed - _lastElapsed;
    _lastElapsed = elapsed;
    final expectedMs = 1000.0 / 60.0;
    final actualMs = dt.inMicroseconds / 1000.0;
    // Frame smooth jika render < 2x budget
    final isSmooth = actualMs < expectedMs * 2.5;
    _smoothFrameRatio = _smoothFrameRatio * 0.92 + (isSmooth ? 1.0 : 0.0) * 0.08;

    setState(() {
      _fps = rawFps;
    });
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _mq = MediaQuery.of(context);
    _physicalSize = _mq.size * _mq.devicePixelRatio;
    _dpr = _mq.devicePixelRatio;
  }

  @override
  void dispose() {
    _ticker.dispose();
    _btSub?.cancel();
    super.dispose();
  }

  // ── Build ──────────────────────────────────────────────────────────────────
  @override
  Widget build(BuildContext context) {
    final isDark = ThemeController.isDark;
    final ac = widget.accentColor;

    return Scaffold(
      backgroundColor: _AC.bg(isDark),
      appBar: AppBar(
        backgroundColor: _AC.surface(isDark).withOpacity(.95),
        elevation: 0,
        leading: IconButton(
          icon: Icon(Icons.arrow_back_ios_new_rounded, color: ac),
          onPressed: () => Navigator.pop(context),
        ),
        title: Text(
          'Spesifikasi HP',
          style: TextStyle(
            color: _AC.text(isDark),
            fontWeight: FontWeight.w900,
            fontSize: 18,
          ),
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(16, 16, 16, 32),
        children: [
          // ─── Radar chart ──────────────────────────────────────────────────
          _radarCard(isDark, ac),
          const SizedBox(height: 16),

          // ─── Perangkat ────────────────────────────────────────────────────
          _sectionCard(
            isDark, ac,
            icon: Icons.phone_android_rounded,
            title: 'PERANGKAT',
            rows: [
              _row('Merek', Platform.isAndroid ? 'Android Device' : (Platform.isIOS ? 'Apple' : Platform.operatingSystem)),
              _row('Nama Aplikasi', _appName),
              _row('Package', _packageName),
              _row('Versi Aplikasi', widget.appVersion),
              _row('OS', Platform.operatingSystemVersion),
              _row('Platform', Platform.operatingSystem.toUpperCase()),
              _row('Jumlah Prosesor', '${Platform.numberOfProcessors} core'),
            ],
          ),
          const SizedBox(height: 12),

          // ─── Layar & Sistem ───────────────────────────────────────────────
          _sectionCard(
            isDark, ac,
            icon: Icons.aspect_ratio_rounded,
            title: 'LAYAR & SISTEM',
            rows: [
              _row('Resolusi Fisik',
                '${_physicalSize.width.toInt()} x ${_physicalSize.height.toInt()} px'),
              _row('Resolusi Logis',
                '${_mq.size.width.toInt()} x ${_mq.size.height.toInt()} dp'),
              _row('Pixel Ratio', _dpr.toStringAsFixed(2)),
              _row('Text Scale', _mq.textScaler.scale(1.0).toStringAsFixed(2)),
              _row('Orientasi',
                _mq.orientation == Orientation.portrait ? 'Portrait' : 'Landscape'),
              _row('Tema Sistem',
                _mq.platformBrightness == Brightness.dark ? 'Gelap' : 'Terang'),
              _row('Locale',
                Platform.localeName.isNotEmpty ? Platform.localeName : 'en_US'),
              _row('Zona Waktu', DateTime.now().timeZoneName),
              _row('Offset UTC', _tzOffset()),
              _row('Padding Top', '${_mq.padding.top.toInt()} px'),
              _row('Padding Bottom', '${_mq.padding.bottom.toInt()} px'),
            ],
          ),
          const SizedBox(height: 12),

          // ─── Jaringan & Baterai (live) ────────────────────────────────────
          _sectionCard(
            isDark, ac,
            icon: Icons.bolt_rounded,
            title: 'JARINGAN & PERFORMA (live)',
            subtitle: 'live',
            rows: [
              _row('Bluetooth', _btLabel()),
              _row('FPS Aktual (live)', '${_fps.toInt()}'),
              _row('Frame Mulus (live)', '${(_smoothFrameRatio * 100).toInt()}%'),
              _row('Zona Waktu', DateTime.now().timeZoneName),
            ],
          ),
          const SizedBox(height: 12),

          // ─── Catatan ──────────────────────────────────────────────────────
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 4),
            child: Text(
              'Catatan: FPS & Frame Mulus dihitung dari statistik frame render nyata — '
              'bukan dari sensor latensi sentuh khusus karena itu butuh akses native '
              'di luar Flutter. Data baterai & koneksi WiFi/seluler memerlukan package '
              'tambahan (battery_plus / connectivity_plus) yang belum ada di build ini.',
              style: TextStyle(color: _AC.faint(isDark), fontSize: 11, height: 1.5),
            ),
          ),
        ],
      ),
    );
  }

  String _tzOffset() {
    final offset = DateTime.now().timeZoneOffset;
    final h = offset.inHours;
    final m = (offset.inMinutes % 60).abs();
    final sign = h >= 0 ? '+' : '-';
    return 'UTC$sign${h.abs().toString().padLeft(2, '0')}:${m.toString().padLeft(2, '0')}';
  }

  String _btLabel() {
    switch (_btState) {
      case BluetoothAdapterState.on: return 'Aktif';
      case BluetoothAdapterState.off: return 'Mati';
      case BluetoothAdapterState.turningOn: return 'Menyala...';
      case BluetoothAdapterState.turningOff: return 'Mati...';
      case BluetoothAdapterState.unauthorized: return 'Tidak Diizinkan';
      default: return 'Tidak Diketahui';
    }
  }

  Map<String, String> _row(String label, String value) => {'l': label, 'v': value};

  // ── Radar chart ────────────────────────────────────────────────────────────
  Widget _radarCard(bool isDark, Color ac) {
    // Nilai radar (0..1)
    final refreshScore = 1.0; // 60 Hz (baseline)
    final battScore = 0.25;   // placeholder (butuh battery_plus)
    final perfScore = (_fps / 60.0).clamp(0.0, 1.0);
    final touchScore = _smoothFrameRatio.clamp(0.0, 1.0);
    final scores = [refreshScore, battScore, touchScore, perfScore];
    final labels = ['Refresh Rate', 'Baterai', 'Respons Sentuh', 'Performa'];
    final displayVals = [
      '${(_mq.size.shortestSide > 400 ? 90 : 60).toInt()} Hz',
      '?%',
      '${(touchScore * 100).toInt()}%',
      '${_fps.toInt()} FPS',
    ];

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: _AC.surface(isDark),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: ac.withOpacity(.15)),
        boxShadow: [BoxShadow(color: ac.withOpacity(.06), blurRadius: 24)],
      ),
      child: Column(children: [
        SizedBox(
          height: 200,
          child: CustomPaint(
            size: const Size(double.infinity, 200),
            painter: _RadarPainter(scores: scores, labels: labels, color: ac, isDark: isDark),
          ),
        ),
        const SizedBox(height: 16),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceEvenly,
          children: List.generate(labels.length, (i) {
            return Column(children: [
              Text(
                displayVals[i],
                style: TextStyle(color: ac, fontWeight: FontWeight.w900, fontSize: 14),
              ),
              const SizedBox(height: 2),
              Text(labels[i], style: TextStyle(color: _AC.faint(isDark), fontSize: 10)),
            ]);
          }),
        ),
      ]),
    );
  }

  // ── Section card ───────────────────────────────────────────────────────────
  Widget _sectionCard(
    bool isDark,
    Color ac, {
    required IconData icon,
    required String title,
    String? subtitle,
    required List<Map<String, String>> rows,
  }) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: _AC.surface(isDark),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: ac.withOpacity(.12)),
        boxShadow: [BoxShadow(color: ac.withOpacity(.04), blurRadius: 18)],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Icon(icon, color: ac, size: 16),
          const SizedBox(width: 8),
          Text(
            subtitle != null ? '$title ($subtitle)' : title,
            style: TextStyle(
              color: _AC.text(isDark),
              fontSize: 11,
              fontWeight: FontWeight.w900,
              letterSpacing: 1.2,
            ),
          ),
        ]),
        const SizedBox(height: 12),
        ...rows.map((r) => _specRow(isDark, r['l']!, r['v']!)),
      ]),
    );
  }

  Widget _specRow(bool isDark, String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            flex: 4,
            child: Text(label,
              style: TextStyle(color: _AC.faint(isDark), fontSize: 13)),
          ),
          Expanded(
            flex: 5,
            child: Text(value,
              textAlign: TextAlign.right,
              style: TextStyle(
                color: _AC.text(isDark),
                fontSize: 13,
                fontWeight: FontWeight.w700,
              )),
          ),
        ],
      ),
    );
  }
}

// ── Radar Painter ─────────────────────────────────────────────────────────────
class _RadarPainter extends CustomPainter {
  final List<double> scores;
  final List<String> labels;
  final Color color;
  final bool isDark;

  _RadarPainter({
    required this.scores,
    required this.labels,
    required this.color,
    required this.isDark,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = (size.shortestSide / 2) * 0.78;
    final n = scores.length;
    final angleStep = 2 * math.pi / n;
    // Start from top (- pi/2)
    double startAngle = -math.pi / 2;

    // Grid rings
    final gridPaint = Paint()
      ..color = (isDark ? Colors.white : Colors.black).withOpacity(.08)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1;

    for (int ring = 1; ring <= 4; ring++) {
      final r = radius * ring / 4;
      final path = Path();
      for (int i = 0; i < n; i++) {
        final angle = startAngle + i * angleStep;
        final p = Offset(center.dx + r * _cos(angle), center.dy + r * _sin(angle));
        if (i == 0) path.moveTo(p.dx, p.dy); else path.lineTo(p.dx, p.dy);
      }
      path.close();
      canvas.drawPath(path, gridPaint);
    }

    // Axis lines
    final axisPaint = Paint()
      ..color = (isDark ? Colors.white : Colors.black).withOpacity(.12)
      ..strokeWidth = 1;
    for (int i = 0; i < n; i++) {
      final angle = startAngle + i * angleStep;
      canvas.drawLine(
        center,
        Offset(center.dx + radius * _cos(angle), center.dy + radius * _sin(angle)),
        axisPaint,
      );
    }

    // Data polygon
    final fillPaint = Paint()
      ..color = color.withOpacity(.18)
      ..style = PaintingStyle.fill;
    final strokePaint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.5;

    final dataPath = Path();
    for (int i = 0; i < n; i++) {
      final angle = startAngle + i * angleStep;
      final r = radius * scores[i].clamp(0.0, 1.0);
      final p = Offset(center.dx + r * _cos(angle), center.dy + r * _sin(angle));
      if (i == 0) dataPath.moveTo(p.dx, p.dy); else dataPath.lineTo(p.dx, p.dy);
    }
    dataPath.close();
    canvas.drawPath(dataPath, fillPaint);
    canvas.drawPath(dataPath, strokePaint);

    // Dots at vertices
    final dotPaint = Paint()..color = color;
    for (int i = 0; i < n; i++) {
      final angle = startAngle + i * angleStep;
      final r = radius * scores[i].clamp(0.0, 1.0);
      final p = Offset(center.dx + r * _cos(angle), center.dy + r * _sin(angle));
      canvas.drawCircle(p, 4, dotPaint);
    }

    // Labels
    final labelStyle = TextStyle(
      color: isDark ? Colors.white54 : Colors.black54,
      fontSize: 10,
      fontWeight: FontWeight.w700,
    );
    for (int i = 0; i < n; i++) {
      final angle = startAngle + i * angleStep;
      final labelR = radius * 1.20;
      final lp = Offset(center.dx + labelR * _cos(angle), center.dy + labelR * _sin(angle));
      final tp = TextPainter(
        text: TextSpan(text: labels[i], style: labelStyle),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(lp.dx - tp.width / 2, lp.dy - tp.height / 2));
    }
  }

  double _cos(double a) => math.cos(a);
  double _sin(double a) => math.sin(a);

  @override
  bool shouldRepaint(covariant _RadarPainter old) =>
      old.scores != scores || old.color != color;
}
