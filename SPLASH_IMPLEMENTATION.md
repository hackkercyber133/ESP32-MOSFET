# VP Energy Splash

Splash sekarang menggunakan video referensi `assets/splash/vp_splash.mp4` secara langsung agar bentuk petir, urutan sambaran, glow, partikel, ring, ledakan, dan loading tampil **100% sama** dengan video yang diberikan.

Video diputar menggunakan `video_player`. Setelah playback mencapai akhir, aplikasi berpindah otomatis ke `ControllerPage` menggunakan transisi fade dan scale singkat. Jika decoder video gagal pada perangkat tertentu, aplikasi tetap melanjutkan ke menu utama.
