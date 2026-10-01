# Ditto Remote artwork

## Current illustrated icon — helper 0.2.1

The user liked the outline but rejected glossy photorealistic jelly. The built-in image generation tool edited the original to preserve the contour and face while replacing the wet highlights with soft cartoon shading. Current source: `AppIcon.png` (local generation source).

Current edit prompt:

Use case: style-transfer. Edit target: the attached current Ditto Remote macOS app icon. Preserve the character's EXACT outer contour, proportions, slight asymmetry, pose, central tall rounded head, raised left nub, lower right nub, broad soft base, face placement, lavender-purple body and pink smile, and charcoal rounded square tile. Change only the visual rendering style: make this a charming clean illustrated app icon with restrained 2.5D cartoon shading. Much less glossy: matte lavender, simple broad tonal shading, flat black dot eyes, clean pink mouth with fine dark outline. Remove wet specular highlights, glassy translucency, photorealistic jelly, realistic rim lighting and reflections. Keep a little gentle volume so it is not a flat logo, but make the drawing friendly, simple and graphic. The charcoal tile should be simple and matte too. No extra props, no text, no outlines around the entire character, no platform pedestal. Keep generous rounded tile corners and preserve actual alpha outside the tile. One finished square icon, 1024x1024 or higher. Match the supplied silhouette precisely; it is already liked.

The result is copied without pixel edits to `AppIcon.png` and packaged into `AppIcon.icns`. `Menu.swift` now traces that same outer contour in the illustration's 1280px coordinates, uniformly scales it to the menu bar, and renders a silhouette without any facial features. Helper 0.2.4 adds a centered cutout connection glyph: USB plug, Bluetooth, or disconnected dash, per user request. The three vector template images are cached and swapped only when the active connection changes. The historical glossy master remains at the original generated path below.

## Earlier glossy version — superseded

Generated September 23, 2026 with the image generation tool for this local prototype. Original: `AppIcon.png` (local generation source).

Prompt:

Use case: stylized-concept. Asset type: finished macOS app icon, 1024x1024 square master, for a personal Ditto microphone/remote helper. A single adorable lavender-purple Ditto Pokemon liquid blob, two tiny black dot eyes and a simple small horizontal smile with pink interior and a fine dark outline, rendered with exquisite soft glossy 3D jelly material, subtle translucent edges, round lumpy shoulders, no legs. Centered on a deep charcoal rounded-square macOS icon tile, exceptionally restrained studio lighting, a broad soft white highlight, no extra objects, no microphone prop, no text, no badge, no cables, no marks. The purple blob fills about 68 percent of tile width and is optically centered, lively and calm with a slight asymmetry. Icon tile has generous rounded corners, straight frontal view, not tilted. Transparent pixels outside the rounded tile, actual alpha, no checkerboard. Legible simple silhouette at 32px. This is a polished app icon, not a scene or mockup.

The icon is packaged with the mac-helper-apps skill's `package_icon.py`. The menu-bar silhouette is drawn in `Menu.swift` and uses template rendering for light/dark appearances.
