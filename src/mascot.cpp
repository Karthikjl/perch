// The mascot's looks (see mascot.h). Everything is drawn in design units and placed by a transform.
#include "mascot.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

const wchar_t* const kMascotKeys[MS_COUNT] = {L"Falcon", L"Ember", L"Mint", L"Cyclops", L"Visor", L"Sunny", L"Moonlight"};
const wchar_t* const kMascotNames[MS_COUNT] = {L"Falcon", L"Ember", L"Mint leaf", L"Cyclops", L"Visor bot", L"Sunny", L"Moonlight"};

namespace {

D2D1_COLOR_F Rgb(unsigned v, float a = 1) { return D2D1::ColorF(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, a); }
float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Path from numbers: x0 y0, then 0 x y (line) or 1 cx cy x y (quadratic curve). mirror flips x about 160.
ID2D1PathGeometry* MakePath(ID2D1Factory* f, std::initializer_list<float> pts, bool mirror, bool closed = true) {
    ID2D1PathGeometry* path = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(f->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) { Release(path); return nullptr; }
    auto it = pts.begin();
    auto X = [&](float v) { return mirror ? 320 - v : v; };
    float x0 = *it++, y0 = *it++;
    sink->BeginFigure(D2D1::Point2F(X(x0), y0), closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    while (it != pts.end()) {
        float tag = *it++;
        if (tag == 0) { float x = *it++, y = *it++; sink->AddLine(D2D1::Point2F(X(x), y)); }
        else { float qx = *it++, qy = *it++, x = *it++, y = *it++; sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(X(qx), qy), D2D1::Point2F(X(x), y))); }
    }
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    return path;
}

// ---- Perch's falcon: a 320 x 56 design (the notch's body is x 18..302, centre 160) ----
enum { FG_FEATHER_L, FG_FEATHER_R, FG_MID_L, FG_MID_R, FG_CHEEK_L, FG_CHEEK_R, FG_STRIPE_L, FG_STRIPE_R, FG_BROW_L, FG_BROW_R,
       FG_SHUT_L, FG_SHUT_R, FG_CERE, FG_CERE_TOP, FG_BEAK, FG_COUNT };
// Shapes belong to the factory that made them; the notch and the Settings window each have one.
struct FalconShapes { ID2D1Factory* f = nullptr; ID2D1PathGeometry* g[FG_COUNT] = {}; ID2D1StrokeStyle* round = nullptr; };
FalconShapes g_cache[2];

FalconShapes* Shapes(ID2D1Factory* f) {
    for (auto& c : g_cache) if (c.f == f && c.g[0]) return &c;
    FalconShapes& c = g_cache[0].f == nullptr || g_cache[0].f == f ? g_cache[0] : g_cache[1];
    for (auto*& g : c.g) Release(g);
    Release(c.round);
    c.f = f;
    for (int m = 0; m < 2; m++) {
        bool r = m == 1;
        c.g[FG_FEATHER_L + m] = MakePath(f, {60, 0, 0, 150, 0, 0, 140, 30, 0, 100, 22}, r);                       // dark hood
        c.g[FG_MID_L + m] = MakePath(f, {72, 8, 1, 100, 6, 120, 14, 0, 104, 20, 1, 88, 16, 72, 8}, r);            // lighter feathers
        c.g[FG_CHEEK_L + m] = MakePath(f, {88, 30, 1, 100, 40, 112, 58, 0, 98, 58, 1, 90, 46, 80, 36}, r);        // white cheek
        c.g[FG_STRIPE_L + m] = MakePath(f, {98, 30, 1, 108, 40, 120, 58, 0, 112, 58, 1, 102, 44, 92, 34}, r);     // teardrop stripe
        c.g[FG_BROW_L + m] = MakePath(f, {102, 13, 1, 120, 4, 144, 17, 0, 141, 22, 1, 121, 13, 104, 19}, r);     // heavy brow: the stare
        c.g[FG_SHUT_L + m] = MakePath(f, {107, 26, 1, 121, 34, 135, 26}, r, false);                               // closed eye
    }
    c.g[FG_CERE] = MakePath(f, {138, 22, 1, 160, 14, 182, 22, 0, 174, 42, 1, 160, 50, 146, 42}, false);
    c.g[FG_CERE_TOP] = MakePath(f, {138, 22, 1, 160, 14, 182, 22, 0, 180, 27, 1, 160, 20, 140, 27}, false);
    c.g[FG_BEAK] = MakePath(f, {149, 40, 1, 160, 35, 171, 40, 0, 163, 64, 0, 160, 70, 0, 157, 64}, false);
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    f->CreateStrokeStyle(sp, nullptr, 0, &c.round);
    for (auto* g : c.g) if (!g) return nullptr;
    return &c;
}

void DrawFalcon(ID2D1RenderTarget* rt, ID2D1Factory* f, ID2D1SolidColorBrush* br, ID2D1Geometry* clip, float cx, float h, float a,
                float open, float look) {
    FalconShapes* c = Shapes(f);
    if (!c) return;
    float s = h / 56;
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(cx - 160 * s, 0) * old;
    auto fill = [&](int g, unsigned rgb, float al) { br->SetColor(Rgb(rgb, a * al)); rt->FillGeometry(c->g[g], br); };
    float o = Clamp01(open), wide = std::max(0.0f, open - 1); // 0 shut .. 1 open, then wider when it spots you
    float dim = 0.8f + 0.2f * o;                             // asleep it fades a little into the dark

    if (clip) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip), nullptr);
    rt->SetTransform(m);
    for (int k = 0; k < 2; k++) {
        fill(FG_FEATHER_L + k, 0x141C33, 0.9f * dim);
        fill(FG_MID_L + k, 0x26325A, 0.9f * dim);
        fill(FG_CHEEK_L + k, 0xE8E6DF, 0.9f * dim);
        fill(FG_STRIPE_L + k, 0x101729, 0.9f * dim);
    }
    for (int k = 0; k < 2; k++) { // eyes: amber ring, black pupil glancing about, a glint; they close by squashing
        float ex = k ? 199.0f : 121.0f, ey = 25, rr = 13.5f * (1 + 0.12f * wide);
        br->SetColor(Rgb(0x141C33, a * dim));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ex, ey), 13.5f, 13.5f), br);
        if (o > 0.02f) {
            br->SetColor(Rgb(0xF2B705, a));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ex, ey), rr, rr * o), br);
            float px = ex + Clamp01(o * 1.5f) * look / s * 0.41f;
            br->SetColor(D2D1::ColorF(0.04f, 0.04f, 0.04f, a));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, ey), 9.9f, 9.9f * o), br);
            br->SetColor(D2D1::ColorF(1, 1, 1, a * Clamp01((o - 0.5f) / 0.3f)));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px + 3.8f, ey - 3.6f * o), 2.7f, 2.7f), br);
        }
        if (o < 0.3f) {
            br->SetColor(Rgb(0xBA7517, a * (1 - o / 0.3f)));
            rt->DrawGeometry(c->g[FG_SHUT_L + k], br, 2.2f, c->round);
        }
        fill(FG_BROW_L + k, 0x141C33, 1);
    }
    fill(FG_CERE, 0xFAC775, dim);
    fill(FG_CERE_TOP, 0xEF9F27, dim);
    br->SetColor(Rgb(0x2C2C2A, a * dim));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(151, 31), 2, 2), br);
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(169, 31), 2, 2), br);
    rt->SetTransform(old);
    if (clip) rt->PopLayer();
    // The beak's tip pokes out under the notch.
    ID2D1GradientStopCollection* stops = nullptr;
    ID2D1LinearGradientBrush* lg = nullptr;
    D2D1_GRADIENT_STOP bs[2] = {{0, Rgb(0x9AA3B8)}, {1, Rgb(0x4A5266)}};
    if (SUCCEEDED(rt->CreateGradientStopCollection(bs, 2, &stops)) &&
        SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 35), D2D1::Point2F(0, 70)), stops, &lg))) {
        rt->SetTransform(m);
        lg->SetOpacity(a * dim);
        rt->FillGeometry(c->g[FG_BEAK], lg);
        rt->SetTransform(old);
    }
    Release(lg);
    Release(stops);
}

// ---- the six faces: drawn in units of the old mock-ups (eyes ~9 apart), centred on the notch ----
void Curve(ID2D1RenderTarget* rt, ID2D1Factory* f, ID2D1SolidColorBrush* br, ID2D1StrokeStyle* round, float x0, float y0, float qx, float qy,
           float x1, float y1, float w) {
    ID2D1PathGeometry* p = MakePath(f, {x0 + 160, y0, 1, qx + 160, qy, x1 + 160, y1}, false, false);
    if (p) rt->DrawGeometry(p, br, w, round);
    Release(p);
}

void DrawFace(ID2D1RenderTarget* rt, ID2D1Factory* f, ID2D1SolidColorBrush* br, int style, float cx, float h, float a, float open,
              float look, float t) {
    FalconShapes* c = Shapes(f);
    if (!c) return;
    ID2D1StrokeStyle* round = c->round;
    float u = 1.15f * h / 30, cy = h / 2 + 0.5f;
    float o = Clamp01(open), wide = std::max(0.0f, open - 1), shut = o < 0.3f ? 1 - o / 0.3f : 0;
    float lx = look * 0.5f / u * Clamp01(o * 1.5f);
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    // Face units: x is offset by 160 (so paths can share MakePath), y = 0 at the eyes' line.
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Translation(-160, 0) * D2D1::Matrix3x2F::Scale(u, u) * D2D1::Matrix3x2F::Translation(cx, cy) * old;
    auto eyeT = [&](float ex, float ey) { // squash an eye about its centre as it opens and closes
        float k = (o > 0.02f ? o : 0.02f) * (1 + 0.12f * wide);
        rt->SetTransform(D2D1::Matrix3x2F::Scale(1 + 0.12f * wide, k, D2D1::Point2F(ex + 160, ey)) * m);
    };
    auto col = [&](unsigned rgb, float al) { br->SetColor(Rgb(rgb, a * al)); };

    if (style == MS_EMBER) {
        for (int k = -1; k <= 1; k += 2) {
            if (o > 0.02f) {
                eyeT(k * 9.0f + lx, -1);
                col(0xFAEEDA, 1);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(160 + k * 9 + lx, -1), 4.5f, 4.5f), br);
                col(0xFFFFFF, Clamp01((o - 0.5f) / 0.3f));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(161.5f + k * 9 + lx, -2.5f), 1.2f, 1.2f), br);
            }
            rt->SetTransform(m);
            if (shut > 0) { col(0xFAC775, shut); Curve(rt, f, br, round, k * 9 - 4.5f, 0, k * 9.0f, 3, k * 9 + 4.5f, 0, 1.6f); }
        }
        col(0xFAEEDA, o);
        Curve(rt, f, br, round, -3, 6, 0, 8.5f, 3, 6, 1.3f);
    } else if (style == MS_MINT) {
        for (int k = -1; k <= 1; k += 2) {
            if (o > 0.02f) {
                eyeT(k * 10.0f + lx, -1);
                col(0xE1F5EE, 1);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(160 + k * 10 + lx, -1), 5.5f, 4), br);
            }
            rt->SetTransform(m);
            if (shut > 0) { col(0x9FE1CB, shut); Curve(rt, f, br, round, k * 10 - 5.0f, -1, k * 10.0f, 2, k * 10 + 5.0f, -1, 1.6f); }
            col(0xED93B1, 0.45f + 0.25f * o);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(160 + k * 16, 5), 3, 1.6f), br);
        }
    } else if (style == MS_CYCLOPS) {
        if (o > 0.02f) {
            eyeT(0, -1);
            col(0xF1EFE8, 1);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(160, -1), 8, 7), br);
            col(0x26215C, 1);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(160 + lx * 1.6f, -0.5f), 3.2f, 3.2f), br);
        }
        rt->SetTransform(m);
        if (shut > 0) { col(0xCECBF6, shut); Curve(rt, f, br, round, -8, -1, 0, 4, 8, -1, 1.8f); }
    } else if (style == MS_VISOR) {
        rt->SetTransform(m);
        col(0x2C2C2A, 1);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(142, -5, 178, 4), 4.5f, 4.5f), br);
        if (o > 0.02f) { // the light scans side to side
            float x = 144 + 22 * (0.5f - 0.5f * cosf(6.2832f * t / 2.4f));
            eyeT(x - 160 + 5, -0.5f);
            col(0xF7C1C1, 1);
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, -3, x + 10, 2), 2.5f, 2.5f), br);
        }
        rt->SetTransform(m);
        if (shut > 0) { col(0xE24B4A, shut * 0.8f); rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(146, -1.5f, 174, 0), 0.75f, 0.75f), br); }
    } else if (style == MS_SUNNY) {
        for (int k = -1; k <= 1; k += 2) { // "> <"
            if (o > 0.02f) {
                eyeT(k * 9.5f + lx, -1);
                col(0xFAEEDA, 1);
                D2D1_POINT_2F a0 = D2D1::Point2F(160 + k * 13 + lx, -5), a1 = D2D1::Point2F(160 + k * 6 + lx, -1), a2 = D2D1::Point2F(160 + k * 13 + lx, 3);
                rt->DrawLine(a0, a1, br, 2, round);
                rt->DrawLine(a1, a2, br, 2, round);
            }
            rt->SetTransform(m);
            if (shut > 0) { col(0xFAC775, shut); rt->DrawLine(D2D1::Point2F(160 + k * 14, 0), D2D1::Point2F(160 + k * 5, 0), br, 1.8f, round); }
        }
        if (wide > 0) { // a sparkle when it spots you
            col(0xFAC775, Clamp01(wide * 4));
            float sx = 180, sy = -9, r1 = 4, r2 = 1;
            ID2D1PathGeometry* p = MakePath(f, {sx, sy - r1, 0, sx + r2, sy - r2, 0, sx + r1, sy, 0, sx + r2, sy + r2, 0, sx, sy + r1, 0, sx - r2, sy + r2,
                                                0, sx - r1, sy, 0, sx - r2, sy - r2}, false);
            if (p) rt->FillGeometry(p, br);
            Release(p);
        }
    } else if (style == MS_MOON) {
        for (int k = -1; k <= 1; k += 2) { // small crescents, thick at the top
            float ex = 160 + k * 9.0f + lx;
            if (o > 0.02f) {
                eyeT(k * 9.0f + lx, 0);
                col(0xF1EFE8, 1);
                ID2D1PathGeometry* p = nullptr;
                ID2D1GeometrySink* sk = nullptr;
                if (SUCCEEDED(f->CreatePathGeometry(&p)) && SUCCEEDED(p->Open(&sk))) {
                    sk->BeginFigure(D2D1::Point2F(ex - 5, 1), D2D1_FIGURE_BEGIN_FILLED);
                    sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(ex + 5, 1), D2D1::SizeF(5, 5), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                    sk->AddArc(D2D1::ArcSegment(D2D1::Point2F(ex - 5, 1), D2D1::SizeF(5, 3), 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                    sk->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sk->Close();
                    rt->FillGeometry(p, br);
                }
                Release(sk);
                Release(p);
            }
            rt->SetTransform(m);
            if (shut > 0) { col(0xB4B2A9, shut); Curve(rt, f, br, round, k * 9 - 5.0f, -1, k * 9.0f, 2.5f, k * 9 + 5.0f, -1, 1.5f); }
        }
    }
    rt->SetTransform(old);
}

} // namespace

void Mascot_Glow(int style, D2D1_COLOR_F stops[3], float* strength) {
    static const unsigned col[MS_COUNT][3] = {{0x854F0B, 0xEF9F27, 0xBA7517}, {0xEF9F27, 0xD85A30, 0xD4537E}, {0x97C459, 0x1D9E75, 0x5DCAA5},
                                              {0xED93B1, 0xAFA9EC, 0x7F77DD}, {0xE24B4A, 0xD85A30, 0xE24B4A}, {0xFAC775, 0xEF9F27, 0x97C459},
                                              {0x888780, 0xD3D1C7, 0x888780}};
    if (style < 0 || style >= MS_COUNT) style = MS_FALCON;
    for (int i = 0; i < 3; i++) stops[i] = Rgb(col[style][i]);
    *strength = style == MS_VISOR || style == MS_MOON ? 0.8f : 1.0f;
}

void Mascot_DrawFace(ID2D1RenderTarget* rt, ID2D1Factory* f, ID2D1SolidColorBrush* br, int style, ID2D1Geometry* clip,
                     float cx, float h, float a, float open, float look, float t) {
    if (a <= 0 || !rt || !f || !br) return;
    if (style <= MS_FALCON || style >= MS_COUNT) DrawFalcon(rt, f, br, clip, cx, h, a, open, look);
    else DrawFace(rt, f, br, style, cx, h, a, open, look, t);
}
