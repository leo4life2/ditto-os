# Screen safe area — photo calibration

**Use a 40 px inset as the tested conservative rectangle. Use 50 px for normal text and controls.**

The supplied inset-guide photo (`IMG_3204`) shows breaks at the corners of the 10, 20 and 30 px guides. The **40 px cyan rectangle is visibly continuous at all four corners**. The 50, 60 and 80 px guides are also inside the visible area. The coordinate-grid photo (`IMG_3205`) corroborates substantial corner clipping rather than a uniformly missing strip around the whole display.

| Region | Coordinates, inclusive | Size | Use |
|---|---|---|---|
| Addressable canvas | x 0–409, y 0–501 | 410 × 502 | Backgrounds/art; expect physical clipping |
| Tested conservative safe rectangle | **x 40–369, y 40–461** | **330 × 422** | Content that must remain visible |
| Recommended default UI rectangle | **x 50–359, y 50–451** | **310 × 402** | Text, essential icons, controls; adds 10 px breathing room |

The physical minimum uniform inset is bracketed between **more than 30 and at most 40 px** by these guides. This is photo-based evidence at 10 px guide spacing, not a pixel-perfect optical measurement. We have not measured the exact corner radius or a per-row visible mask. The 50 px recommendation is a design margin, not additional observed bezel loss.

Keep the *whole visual extent* of important elements inside the chosen rectangle, including glyph overhang, outlines, shadows and focus states. Backgrounds and decorative animation can use the full canvas. Edge-aware layouts can reclaim more space around the middle of each edge; the 40 px rule is a simple safe rectangle, not a claim that all outer pixels are invisible.

[Machine-readable geometry](screen-geometry.json) uses half-open bounds: safe `[40,40,370,462)`, default UI `[50,50,360,452)`.

Evidence: [inset-guide photo](IMG_3204.png) · [numbered-grid photo](IMG_3205.png). PNGs are viewing conversions of the supplied HEIC files; neither the displayed guide layout nor photo geometry was altered.
