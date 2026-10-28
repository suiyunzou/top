#pragma once
#include "model.hpp"
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellapi.h>
#include <wincodec.h>
template <class T> inline void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}
inline bool light(bool taskbar = false) {
  DWORD v = 0, n = 4;
  return RegGetValueW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\P"
                      L"ersonalize",
                      taskbar ? L"SystemUsesLightTheme" : L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                      nullptr, &v, &n) == ERROR_SUCCESS &&
         v;
}
inline D2D1_COLOR_F color(unsigned rgb, float alpha = 1) { return D2D1::ColorF(rgb, alpha); }
inline D2D1_COLOR_F temperatureColor(double celsius, bool dark, D2D1_COLOR_F unknown) {
  if (!std::isfinite(celsius) || celsius < 0) return unknown;
  if (celsius >= 80) return color(dark ? 0xFF7973 : 0xD92D20);
  if (celsius >= 65) return color(dark ? 0xFFBC66 : 0xBC6500);
  if (celsius >= 50) return color(dark ? 0x4FD17F : 0x168344);
  return color(dark ? 0x72AEFF : 0x1F5FE0);
}
inline D2D1_COLOR_F fanColor(double ratio, bool dark, D2D1_COLOR_F unknown) {
  if (ratio < 0) return unknown;
  const bool high = ratio > .9;
  const float t = float(high ? bounded((ratio - .9) / .1, 0., 1.)
                            : bounded((ratio - .4) / .3, 0., 1.));
  // Interpolate hue through cyan/green/amber, avoiding a gray midpoint.
  const float hue = high ? (dark ? 33.f - 30.f*t : 30.f - 30.f*t)
                         : (dark ? 215.f - 182.f*t : 220.f - 190.f*t);
  const float saturation = dark ? (high ? .60f - .04f*t : .59f + .01f*t)
                                : (high ? .96f - .20f*t : .82f + .14f*t);
  const float value = dark ? 1.f : (high ? .78f + .08f*t : .88f - .10f*t);
  const float chroma = value*saturation, m = value-chroma;
  const float x = chroma*(1-std::abs(std::fmod(hue/60.f,2.f)-1));
  if (hue < 60) return D2D1::ColorF(chroma+m,x+m,m,1);
  if (hue < 120) return D2D1::ColorF(x+m,chroma+m,m,1);
  if (hue < 180) return D2D1::ColorF(m,chroma+m,x+m,1);
  return D2D1::ColorF(m,x+m,chroma+m,1);
}
struct Theme {
  bool dark = true;
  D2D1_COLOR_F bg, ink, secondary, track, card, cpu, eff, gpu, green;
  void update(const Config &c) {
    auto a = c.get<std::string>("appearance", "auto");
    dark = a == "dark" || (a == "auto" && !light());
    bg = color(dark ? 0x191C27 : 0xEFF1F7);
    ink = color(dark ? 0xF3F4F6 : 0x111827);
    secondary = color(dark ? 0xA5A8B3 : 0x656A74);
    track = color(dark ? 0x303642 : 0xD5D9E2);
    card = color(dark ? 0x292D38 : 0xF8F9FC);
    green = color(dark ? 0x4FD17F : 0x1C9A50);
    auto p = c.get<std::string>("palette", "sea");
    cpu = color(p == "graphite" ? (dark ? 0xF3F4F6 : 0x1F2937)
                : p == "warm"   ? (dark ? 0xFF8468 : 0xDC4A2B)
                                : (dark ? 0x5B9BFF : 0x1F5FE0));
    eff = color(p == "graphite" ? 0x9AA1AD : p == "warm" ? 0xFFC857 : 0x45D1BD);
    gpu = color(p == "graphite" ? 0xC9B98F : p == "warm" ? 0xFF8FC4 : 0xB294FF);
  }
};
struct Canvas {
  ID2D1Factory *factory = nullptr;
  IDWriteFactory *write = nullptr;
  IWICImagingFactory *wic = nullptr;
  ID2D1DCRenderTarget *rt = nullptr;
  ID2D1SolidColorBrush *brush = nullptr;
  ID2D1PathGeometry *fanBlade = nullptr;
  ID2D1StrokeStyle *dashStroke = nullptr;
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ old = nullptr;
  void *pixels = nullptr;
  int width = 0, height = 0;
  std::map<std::pair<int, bool>, IDWriteTextFormat *> fonts;
  std::map<std::wstring, ID2D1Bitmap *> icons;
  struct TextEntry {
    std::wstring text;
    float width, height, size;
    bool bold;
    int align;
    IDWriteTextLayout *layout;
    uint64_t used;
  };
  std::vector<TextEntry> textLayouts;
  uint64_t textClock = 0;
  Canvas() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown **>(&write));
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
  }
  ~Canvas() {
    clear();
    release(fanBlade);
    release(dashStroke);
    for (auto &p : fonts)
      release(p.second);
    release(wic);
    release(write);
    release(factory);
  }
  void clear() {
    for (auto &entry : textLayouts) release(entry.layout);
    textLayouts.clear();
    for (auto &p : icons)
      release(p.second);
    icons.clear();
    release(brush);
    release(rt);
    if (dc) {
      SelectObject(dc, old);
      DeleteObject(bitmap);
      DeleteDC(dc);
    }
    dc = nullptr;
    bitmap = nullptr;
    pixels = nullptr;
    width = height = 0;
  }
  bool prepare(int w, int h) {
    if (dc && width == w && height == h)
      return true;
    clear();
    if (!factory || !write || w < 1 || h < 1)
      return false;
    BITMAPINFO b{};
    b.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    b.bmiHeader.biWidth = w;
    b.bmiHeader.biHeight = -h;
    b.bmiHeader.biPlanes = 1;
    b.bmiHeader.biBitCount = 32;
    b.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(nullptr);
    bitmap = CreateDIBSection(dc, &b, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap) {
      clear();
      return false;
    }
    old = SelectObject(dc, bitmap);
    width = w;
    height = h;
    auto p = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(factory->CreateDCRenderTarget(&p, &rt))) {
      clear();
      return false;
    }
    RECT rect = {0, 0, w, h};
    rt->BindDC(dc, &rect);
    rt->CreateSolidColorBrush(color(0), &brush);
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return brush != nullptr;
  }
  void begin(float scale = 1) {
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->Clear(D2D1::ColorF(0, 0));
    rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
  }
  void fill(float x, float y, float w, float h, D2D1_COLOR_F c, float radius = 0) {
    if (w <= 0 || h <= 0)
      return;
    brush->SetColor(c);
    auto r = D2D1::RectF(x, y, x + w, y + h);
    if (radius > 0)
      rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush);
    else
      rt->FillRectangle(r, brush);
  }
  void border(float x, float y, float w, float h, D2D1_COLOR_F c, float radius = 0,
              float line = 1) {
    brush->SetColor(c);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius),
                             brush, line);
  }
  void line(float x, float y, float a, float b, D2D1_COLOR_F c, float width = 1.3f) {
    brush->SetColor(c);
    rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(a, b), brush, width);
  }
  void circle(float x, float y, float r, D2D1_COLOR_F c, bool stroke = false, float width = 1) {
    brush->SetColor(c);
    auto e = D2D1::Ellipse(D2D1::Point2F(x, y), r, r);
    if (stroke)
      rt->DrawEllipse(e, brush, width);
    else
      rt->FillEllipse(e, brush);
  }
  void bezier(D2D1_POINT_2F start, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F end,
              D2D1_COLOR_F col, float width = 2, bool dashed = false) {
    ID2D1PathGeometry *path = nullptr;
    ID2D1GeometrySink *sink = nullptr;
    if (FAILED(factory->CreatePathGeometry(&path))) return;
    if (FAILED(path->Open(&sink))) { release(path); return; }
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(D2D1::BezierSegment(a, b, end));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    HRESULT hr = sink->Close(); release(sink);
    if (dashed && !dashStroke) {
      auto properties = D2D1::StrokeStyleProperties(); properties.dashStyle = D2D1_DASH_STYLE_DASH;
      factory->CreateStrokeStyle(properties, nullptr, 0, &dashStroke);
    }
    brush->SetColor(col);
    if (SUCCEEDED(hr)) rt->DrawGeometry(path, brush, width, dashed ? dashStroke : nullptr);
    release(path);
  }
  void ringSegment(float x, float y, float outer, float inner, float from, float to,
                   D2D1_COLOR_F fillColor, D2D1_COLOR_F edgeColor) {
    ID2D1PathGeometry *path = nullptr;
    ID2D1GeometrySink *sink = nullptr;
    if (FAILED(factory->CreatePathGeometry(&path))) return;
    if (FAILED(path->Open(&sink))) { release(path); return; }
    auto point = [&](float radius, float degrees) {
      float angle = degrees * 3.14159265f / 180;
      return D2D1::Point2F(x + radius * std::cos(angle), y + radius * std::sin(angle));
    };
    sink->BeginFigure(point(outer, from), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(point(outer, to), D2D1::SizeF(outer, outer), 0,
                                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine(point(inner, to));
    sink->AddArc(D2D1::ArcSegment(point(inner, from), D2D1::SizeF(inner, inner), 0,
                                D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    HRESULT hr = sink->Close(); release(sink);
    if (SUCCEEDED(hr)) {
      brush->SetColor(fillColor); rt->FillGeometry(path, brush);
      brush->SetColor(edgeColor); rt->DrawGeometry(path, brush, 1);
    }
    release(path);
  }
  void fan(float x, float y, float angle, D2D1_COLOR_F c) {
    if (!fanBlade) {
      ID2D1GeometrySink *sink = nullptr;
      if (FAILED(factory->CreatePathGeometry(&fanBlade))) return;
      if (FAILED(fanBlade->Open(&sink))) { release(fanBlade); return; }
      sink->BeginFigure(D2D1::Point2F(0, -1.5f), D2D1_FIGURE_BEGIN_FILLED);
      sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(-2, -3), D2D1::Point2F(-4, -7), D2D1::Point2F(-1, -8)));
      sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(4, -9), D2D1::Point2F(7, -3), D2D1::Point2F(2, -2)));
      sink->EndFigure(D2D1_FIGURE_END_CLOSED);
      HRESULT hr = sink->Close(); release(sink);
      if (FAILED(hr)) { release(fanBlade); return; }
    }
    D2D1_MATRIX_3X2_F original;
    rt->GetTransform(&original);
    brush->SetColor(c);
    for (int i = 0; i < 3; ++i) {
      rt->SetTransform(D2D1::Matrix3x2F::Rotation(angle + i * 120) *
                       D2D1::Matrix3x2F::Translation(x + 10, y + 10) * original);
      rt->FillGeometry(fanBlade, brush);
    }
    rt->SetTransform(original);
    circle(x + 10, y + 10, 1.7f, c);
  }
  void arc(float x, float y, float radius, double value, D2D1_COLOR_F c) {
    int steps = std::max(1, int(bounded(value, 0., 100.) * .4));
    float end = float(bounded(value, 0., 100.) / 100 * 6.283185307);
    for (int i = 0; i < steps; ++i) {
      float a = -1.570796f + end * i / steps, b = -1.570796f + end * (i + 1) / steps;
      line(x + cosf(a) * radius, y + sinf(a) * radius, x + cosf(b) * radius, y + sinf(b) * radius,
           c, 2.2f);
    }
  }
  IDWriteTextFormat *textFormat(float size, bool bold = false) {
    if (!write) return nullptr;
    auto key = std::make_pair(int(size * 10), bold);
    auto &fmt = fonts[key];
    if (!fmt) {
      write->CreateTextFormat(
          L"Segoe UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &fmt);
      if (!fmt)
        return nullptr;
      fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
      IDWriteInlineObject *ellipsis = nullptr;
      write->CreateEllipsisTrimmingSign(fmt, &ellipsis);
      fmt->SetTrimming(&trim, ellipsis);
      release(ellipsis);
    }
    return fmt;
  }
  float textWidth(const std::wstring &s, float size) {
    auto layout = textLayout(s, 4096, 128, size, false, 0);
    DWRITE_TEXT_METRICS metrics{};
    if (layout) layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
  }
  IDWriteTextLayout *textLayout(const std::wstring &s, float w, float h, float size,
                              bool bold, int align) {
    for (auto &entry : textLayouts)
      if (entry.text == s && entry.width == w && entry.height == h && entry.size == size &&
          entry.bold == bold && entry.align == align) {
        entry.used = ++textClock;
        return entry.layout;
      }
    auto fmt = textFormat(size, bold);
    if (!fmt) return nullptr;
    fmt->SetTextAlignment(align == 2   ? DWRITE_TEXT_ALIGNMENT_TRAILING
                          : align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER
                                       : DWRITE_TEXT_ALIGNMENT_LEADING);
    IDWriteTextLayout *layout = nullptr;
    if (FAILED(write->CreateTextLayout(s.c_str(), UINT32(s.size()), fmt, w, h, &layout)))
      return nullptr;
    // Reuse layout between animation frames; changing readings cannot grow the cache indefinitely.
    TextEntry next{s, w, h, size, bold, align, layout, ++textClock};
    if (textLayouts.size() < 192) textLayouts.push_back(std::move(next));
    else {
      auto oldest = std::min_element(textLayouts.begin(), textLayouts.end(),
          [](const TextEntry &a, const TextEntry &b) { return a.used < b.used; });
      release(oldest->layout);
      *oldest = std::move(next);
    }
    return layout;
  }
  void text(const std::wstring &s, float x, float y, float w, float h, float size, D2D1_COLOR_F c,
            bool bold = false, int align = 0) {
    if (w <= 0 || h <= 0) return;
    auto layout = textLayout(s, w, h, size, bold, align);
    if (!layout) return;
    brush->SetColor(c);
    rt->DrawTextLayout(D2D1::Point2F(x, y), layout, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
  void icon(std::wstring key, HICON h, float x, float y, float size, float opacity = 1) {
    if (!h || !wic)
      return;
    auto &b = icons[key];
    if (!b) {
      IWICBitmap *wb = nullptr;
      if (SUCCEEDED(wic->CreateBitmapFromHICON(h, &wb))) {
        IWICFormatConverter *converted = nullptr;
        if (SUCCEEDED(wic->CreateFormatConverter(&converted)) &&
            SUCCEEDED(converted->Initialize(wb, GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapDitherTypeNone, nullptr, 0,
                                            WICBitmapPaletteTypeCustom)))
          rt->CreateBitmapFromWicBitmap(converted, nullptr, &b);
        release(converted);
        release(wb);
      }
    }
    if (b)
      rt->DrawBitmap(b, D2D1::RectF(x, y, x + size, y + size), opacity,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  }
  void glyph(const std::string &k, float x, float y, float size, D2D1_COLOR_F c) {
    auto saved = D2D1::Matrix3x2F::Identity();
    rt->GetTransform(&saved);
    rt->SetTransform(D2D1::Matrix3x2F::Scale(size / 16, size / 16) *
                     D2D1::Matrix3x2F::Translation(x, y) * saved);
    if (k == "cpu" || k == "memory" || k == "gpuMem") {
      border(3, 4, 10, 8, c, 1, 1.4);
      for (int i : {5, 8, 11}) {
        line(float(i), 2, float(i), 4, c);
        line(float(i), 12, float(i), 14, c);
      }
      if (k == "cpu") {
        border(5, 6, 6, 4, c);
        for (int i : {6, 10}) {
          line(1, float(i), 3, float(i), c);
          line(13, float(i), 15, float(i), c);
        }
      }
    } else if (k == "down" || k == "up" || k == "network") {
      if (k == "network") {
        line(4, 2, 4, 14, c);
        line(1, 5, 4, 2, c);
        line(4, 2, 7, 5, c);
        line(12, 2, 12, 14, c);
        line(9, 11, 12, 14, c);
        line(12, 14, 15, 11, c);
      } else {
        float a = k == "down" ? 2 : 14, b = k == "down" ? 13 : 3, v = k == "down" ? 8 : 8;
        line(8, a, 8, b, c);
        line(3, v, 8, b, c);
        line(8, b, 13, v, c);
      }
    } else if (k == "gpu") {
      for (float yy : {2.f, 6.f, 10.f}) {
        line(2, yy + 2, 8, yy - 1, c);
        line(8, yy - 1, 14, yy + 2, c);
        line(14, yy + 2, 8, yy + 5, c);
        line(8, yy + 5, 2, yy + 2, c);
      }
    } else if (k == "disk" || k == "diskRead" || k == "diskWrite") {
      border(2, 3, 12, 10, c, 2);
      line(3, 10, 13, 10, c);
      if (k == "disk") circle(11, 11.5f, .7f, c);
      else {
        float a = k == "diskRead" ? 4.f : 8.f, b = k == "diskRead" ? 8.f : 4.f;
        line(8, a, 8, b, c); line(5, 6, 8, b, c); line(8, b, 11, 6, c);
      }
    } else if (k == "power") {
      border(3, 4, 10, 8, c);
      line(1, 13, 15, 13, c);
    } else if (k == "temperature") {
      border(6, 1, 4, 10, c, 2);
      circle(8, 11.5, 3.5, c, true);
      line(8, 5, 8, 11, c);
    } else if (k == "pin") {
      line(4, 2, 12, 2, c);
      line(6, 2, 6, 7, c);
      line(10, 2, 10, 7, c);
      line(6, 7, 3, 11, c);
      line(3, 11, 13, 11, c);
      line(13, 11, 10, 7, c);
      line(8, 11, 8, 15, c);
    } else if (k == "tray") {
      border(1, 3, 14, 11, c, 2);
      line(1, 6, 15, 6, c);
    } else if (k == "expand") {
      line(2, 6, 2, 2, c); line(2, 2, 6, 2, c);
      line(10, 2, 14, 2, c); line(14, 2, 14, 6, c);
      line(14, 10, 14, 14, c); line(14, 14, 10, 14, c);
      line(6, 14, 2, 14, c); line(2, 14, 2, 10, c);
    } else if (k == "pause") {
      fill(4, 2, 3, 12, c, 1); fill(10, 2, 3, 12, c, 1);
    } else if (k == "play") {
      line(4, 2, 13, 8, c, 1.5); line(13, 8, 4, 14, c, 1.5); line(4, 14, 4, 2, c, 1.5);
    } else if (k == "live") {
      circle(8, 8, 3, c); circle(8, 8, 6, c, true);
    } else if (k == "close") {
      line(4, 4, 12, 12, c, 1.5); line(12, 4, 4, 12, c, 1.5);
    } else if (k == "menu") {
      for (float xx : {3.f, 8.f, 13.f}) circle(xx, 8, 1.3f, c);
    } else if (k == "hide") {
      circle(8, 8, 5, c, true);
      line(2, 2, 14, 14, c);
    } else if (k == "quit") {
      arc(8, 8, 6, 85, c);
      line(8, 1, 8, 8, c);
    } else if (k == "more" || k == "less") {
      float y1 = k == "more" ? 5 : 11, y2 = k == "more" ? 11 : 5;
      line(2, y1, 8, y2, c);
      line(8, y2, 14, y1, c);
    } else if (k == "processes") {
      for (float yy : {3.f, 8.f, 13.f}) {
        circle(2, yy, 1, c);
        line(6, yy, 15, yy, c);
      }
    } else if (k == "estimatePower") {
      for (float yy : {6.f, 10.f})
        for (int i = 0; i < 12; ++i)
          line(float(i + 2), yy + 1.4f * sinf(i * .52f), float(i + 3), yy + 1.4f * sinf((i + 1) * .52f), c);
    } else if (k == "motion") {
      for (int i = 0; i < 14; ++i)
        line(float(i + 1), 8 + 3 * sinf(float(i) * .5f), float(i + 2),
             8 + 3 * sinf(float(i + 1) * .5f), c);
    } else
      circle(8, 8, 4, c, true);
    rt->SetTransform(saved);
  }
  void powerGlyph(const std::string &kind, float x, float y, float size, D2D1_COLOR_F color, double phase) {
    if (phase < 0) { glyph(kind, x, y, size, color); return; }
    auto saved = D2D1::Matrix3x2F::Identity(); rt->GetTransform(&saved);
    rt->SetTransform(D2D1::Matrix3x2F::Scale(size / 16, size / 16) * D2D1::Matrix3x2F::Translation(x, y) * saved);
    auto wave = [&](double offset) { return float(.5 - .5 * cos((phase + offset) * 6.283185307)); };
    auto glow = [&](double offset) { auto c = color; c.a = .3f + .7f * wave(offset); return c; };
    if (kind == "gpu") {
      for (int i = 0; i < 3; ++i) {
        float yy = 2.f + i * 4 + 1.6f * (wave(i * .25) - .5f); auto c = glow(i * .25);
        line(2, yy + 2, 8, yy - 1, c); line(8, yy - 1, 14, yy + 2, c);
        line(14, yy + 2, 8, yy + 5, c); line(8, yy + 5, 2, yy + 2, c);
      }
    } else if (kind == "cpu") {
      border(3, 4, 10, 8, color, 1, 1.2f);
      for (int i = 0; i < 3; ++i) {
        float xx = 5.f + i * 3;
        line(xx, 2, xx, 4, color); line(xx, 12, xx, 14, color);
        float h = 1.5f + 4.5f * wave(i * .25);
        fill(4.5f + i * 2.6f, 11 - h, 1.8f, h, color, .5f);
      }
      line(1, 6, 3, 6, color); line(13, 6, 15, 6, color);
      line(1, 10, 3, 10, color); line(13, 10, 15, 10, color);
    } else {
      glyph(kind, 0, 0, 16, color);
      if (kind == "memory")
        for (int i = 0; i < 3; ++i) fill(4.5f + i * 3, 6, 2, 4, glow(i * .25), .5f);
      else {
        float t = float(phase * 36), px, py;
        if (t < 10) { px = 3 + t; py = 4; }
        else if (t < 18) { px = 13; py = 4 + t - 10; }
        else if (t < 28) { px = 13 - (t - 18); py = 12; }
        else { px = 3; py = 12 - (t - 28); }
        circle(px, py, 2, color);
      }
    }
    rt->SetTransform(saved);
  }
  void activityGlyph(const std::string &kind, float x, float y, float size, D2D1_COLOR_F color, double phase) {
    if (phase < 0) { glyph(kind, x, y, size, color); return; }
    float wave = float(sin(phase * 6.283185307));
    if (kind == "down" || kind == "up") {
      glyph(kind, x, y + (kind == "down" ? 1 : -1) * wave * size * .12f, size, color);
    } else {
      glyph(kind, x, y, size, color);
      auto c = color; c.a = .25f + .75f * (.5f + .5f * wave);
      fill(x + size * .58f, y + size * .69f, size * .19f, size * .12f, c, .5f);
    }
  }
  bool end(HWND hwnd) {
    HRESULT hr = rt->EndDraw();
    if (hr == HRESULT(D2DERR_RECREATE_TARGET)) {
      clear();
      return false;
    }
    if (FAILED(hr))
      return false;
    RECT r;
    GetWindowRect(hwnd, &r);
    // A sent sizing message can arrive while a COM drawing call pumps messages.
    // Never let an old back buffer resize the HWND back to its previous size.
    if (r.right - r.left != width || r.bottom - r.top != height) {
      InvalidateRect(hwnd, nullptr, FALSE);
      return false;
    }
    POINT pt = {r.left, r.top}, src = {0, 0};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    return UpdateLayeredWindow(hwnd, nullptr, &pt, &size, dc, &src, 0, &blend, ULW_ALPHA) != FALSE;
  }
};
inline void roundWindow(HWND hwnd, float dpi) {
  RECT r;
  GetClientRect(hwnd, &r);
  int radius = int(std::min(18.f * dpi, std::min(r.right, r.bottom) / 2.f));
  SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, r.right + 1, r.bottom + 1, radius * 2, radius * 2),
               TRUE);
}
inline void backdrop(HWND hwnd, bool enable, bool dark) {
  int d = dark;
  DwmSetWindowAttribute(hwnd, 20, &d, 4);
  int corner = 2;
  DwmSetWindowAttribute(hwnd, 33, &corner, 4);
  int type = enable ? 3 : 1;
  if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 38, &type, 4)))
    return;
  struct Accent {
    int state, flags, color, animation;
  } a{enable ? 3 : 0, 0, 0, 0};
  struct Data {
    int attr;
    void *data;
    SIZE_T size;
  } data{19, &a, sizeof(a)};
  using Fn = BOOL(WINAPI *)(HWND, Data *);
  auto f = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
  if (f)
    f(hwnd, &data);
}
inline HICON trayTextIcon(const std::wstring &value, int size, bool isLight) {
  // Use a 12-DIP regular taskbar text size, scaled with the 16-DIP tray slot.
  // Only condense wide readings; keep the same height as the value changes.
  const int inset = std::max(1, size / 10), scratchSize = size * 3;
  HDC dc = CreateCompatibleDC(nullptr);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = scratchSize;
  bi.bmiHeader.biHeight = -scratchSize;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void *bits = nullptr;
  auto bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp) { DeleteDC(dc); return nullptr; }
  auto old = SelectObject(dc, bmp);
  HFONT font = nullptr;
  HGDIOBJ oldf = nullptr;
  int fontHeight = std::max(1, MulDiv(12, size, 16)), fontWidth = 0;
  const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
  for (;;) {
    font = CreateFontW(-fontHeight, fontWidth, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                      DEFAULT_PITCH, L"Segoe UI");
    oldf = SelectObject(dc, font);
    int inkHeight = 0, inkLeft = size * 3, inkRight = 0, advance = 0;
    GLYPHMETRICS glyph{};
    for (wchar_t digit = L'0'; digit <= L'9'; ++digit)
      if (GetGlyphOutlineW(dc, digit, GGO_METRICS, &glyph, 0, nullptr, &identity) != GDI_ERROR)
        inkHeight = std::max(inkHeight, int(glyph.gmBlackBoxY));
    for (auto ch : value)
      if (GetGlyphOutlineW(dc, ch, GGO_METRICS, &glyph, 0, nullptr, &identity) != GDI_ERROR) {
        inkLeft = std::min(inkLeft, advance + int(glyph.gmptGlyphOrigin.x));
        inkRight = std::max(inkRight, advance + int(glyph.gmptGlyphOrigin.x + glyph.gmBlackBoxX));
        advance += glyph.gmCellIncX;
      }
    if (inkHeight > size - 2 * inset && fontHeight > 6) {
      --fontHeight;
    } else if (inkRight - inkLeft > size - 2 * inset && fontWidth != 1) {
      TEXTMETRICW metrics{};
      GetTextMetricsW(dc, &metrics);
      fontWidth = std::max(1, (fontWidth ? fontWidth : int(metrics.tmAveCharWidth)) - 1);
    } else break;
    SelectObject(dc, oldf);
    DeleteObject(font);
  }
  memset(bits, 0, scratchSize * scratchSize * 4);
  SetTextColor(dc, RGB(255, 255, 255));
  SetBkColor(dc, RGB(0, 0, 0));
  TextOutW(dc, size, size, value.c_str(), int(value.size()));
  GdiFlush(); // Finish queued drawing before accessing DIB pixels on the CPU.
  auto source = static_cast<DWORD *>(bits);
  int left = scratchSize, top = scratchSize, right = 0, bottom = 0;
  for (int y = 0; y < scratchSize; ++y)
    for (int x = 0; x < scratchSize; ++x)
      if (source[y * scratchSize + x] & 255) {
        left = std::min(left, x); right = std::max(right, x + 1);
        top = std::min(top, y); bottom = std::max(bottom, y + 1);
      }
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  void *iconBits = nullptr;
  HBITMAP iconBitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &iconBits, nullptr, 0);
  if (iconBitmap) {
    auto pixels = static_cast<DWORD *>(iconBits);
    memset(pixels, 0, size * size * 4);
    const int width = right - left, height = bottom - top;
    const int x0 = (size - width) / 2;
    // Center the ink; put an odd spare pixel above it to align with clock text.
    const int y0 = (size - height + 1) / 2;
    const unsigned fg = isLight ? 24 : 250;
    for (int y = 0; y < height && y0 + y < size; ++y)
      for (int x = std::max(0, -x0); x < width && x0 + x < size; ++x) {
        unsigned a = source[(top + y) * scratchSize + left + x] & 255, v = fg * a / 255;
        pixels[(y0 + y) * size + x0 + x] = (a << 24) | (v << 16) | (v << 8) | v;
      }
  }
  SelectObject(dc, oldf);
  DeleteObject(font);
  std::vector<BYTE> maskBits(((size + 15) / 16) * 2 * size, 0);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
  ICONINFO ii{TRUE, 0, 0, mask, iconBitmap};
  HICON icon = iconBitmap ? CreateIconIndirect(&ii) : nullptr;
  DeleteObject(mask);
  DeleteObject(iconBitmap);
  SelectObject(dc, old);
  DeleteObject(bmp);
  DeleteDC(dc);
  return icon;
}
inline HICON trayIcon(double value, int size, bool isLight) {
  return trayTextIcon(num(value), size, isLight);
}
