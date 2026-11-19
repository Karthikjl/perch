// The mascot's looks: Perch's falcon and six faces, each with its own glow. Shared by the notch and the
// Settings window's previews. The notch decides when it shows and how open its eyes are.
#pragma once
#include <d2d1.h>

enum MascotStyle { MS_FALCON = 0, MS_EMBER, MS_MINT, MS_CYCLOPS, MS_VISOR, MS_SUNNY, MS_MOON, MS_COUNT };
extern const wchar_t* const kMascotKeys[MS_COUNT];  // settings.ini words: Falcon, Ember, Mint, ...
extern const wchar_t* const kMascotNames[MS_COUNT]; // names in Settings

// The glow along the bottom of the notch: three colours left to right, and how bright it is (0..1).
void Mascot_Glow(int style, D2D1_COLOR_F stops[3], float* strength);

// Draws the face (not the glow) in a closed notch whose outline is `clip` (null: no clipping), centred on cx,
// h DIPs tall. a = opacity, open = eyes (0 shut, 1 open, up to ~1.45 wide), look = DIPs the eyes glance
// sideways, t = seconds, for the visor's scan. The falcon's beak pokes out up to h / 4 below the notch.
void Mascot_DrawFace(ID2D1RenderTarget* rt, ID2D1Factory* f, ID2D1SolidColorBrush* br, int style, ID2D1Geometry* clip,
                     float cx, float h, float a, float open, float look, float t);
