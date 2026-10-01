# Home layout — firmware 0.9.2

Home now shows only the clock, date, and glossy Ditto. Both home caption lines are omitted in all conversation/sleep states. The home scene has no top/bottom opacity fades. Ditto is positioned 18 native pixels lower, with 52 pixels beneath the settled outline in the measured framebuffer (placement varies naturally with gravity and animation).

The live drawing region now continues through the old footer area, avoiding a cropped lower silhouette. The immutable-framebuffer/TE presentation path and QSPI timing remain unchanged. Touch picking uses the new display offset; touch coordinates passed to physics and expressive behavior are translated back to the existing interaction frame. Shader, geometry asset, bulk physics and force coefficients are unchanged. Other OS settings screens retain their existing captions and text-contrast treatment.

Verified on the actual device: home capture, return from Volume, center and newly exposed lower-body hit tests, sleep settling/freeze at 10% brightness, touch wake and restored home. One continuous boot, no display/audio errors or TE/scan deadline misses in the sampled run. Touch errors: 0. Presented idle rate: 26.98 FPS in this short check; this does not establish the earlier 30 FPS target.

Framebuffer review: `home-after.png`, `home-returned.png`, `volume-after.png`, `sleep-after.png`. Sleep framebuffer pixels are captured before the panel brightness setting, so they do not depict the physical 10% luminance.

Build and flash passed; the flashed image hash was verified by esptool. Prior sources and app binary are retained under `before/`.
