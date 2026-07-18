#pragma once
#include "wled.h"
#include <WiFiClientSecure.h>

/*
 * Weather + Tide dashboard, implemented as a selectable WLED 2D effect
 * ("Weather" in the effects list) plus a background data-fetcher.
 *
 *   Bar 1 (T): outdoor temperature; arrow = warmer/colder 24h from now
 *   Bar 2 (P): chance of rain (precip %) over next 6h; arrow = following 18h more/less rain
 *   Bar 3 (W): tide level (live NOAA water level); arrow = rising(in)/falling(out)
 *   Bar 4 (A): air quality (US AQI, taller = worse); arrow = worsening/improving 24h out
 *
 * Colors: each bar's fill uses its own hand-tuned gradient (GRAD_* below), indexed
 * by the bar's value. The arrow's SHAPE shows the trend direction; the arrow's
 * COLOR encodes the future value (temp/rain/AQI from their gradients; tide arrow is
 * pale blue rising / pale yellow falling). Every pixel is white-balanced by the
 * segment's CCT ("white balance") slider (computeWB) since this RGB panel would
 * otherwise ignore it.
 *
 * Data: Open-Meteo weather + air-quality over plain HTTP (no TLS); NOAA over one
 * insecure HTTPS call.
 * The usermod's loop() refreshes data every WT_REFRESH_MS into member fields.
 * The effect renders those fields; select it on a segment to show the dashboard.
 *
 * Design coords are upright (x=0 left, y=0 top) then mapped to the panel with a
 * 90-degrees-CCW rotation to match this wall's wiring.
 */

#ifndef WT_LAT
#define WT_LAT 41.3540
#endif
#ifndef WT_LON
#define WT_LON -71.9662
#endif
#ifndef WT_TIDE_STATION
#define WT_TIDE_STATION "8461490"     // NOAA New London (nearest live sensor to Mystic)
#endif
#ifndef WT_REFRESH_MS
#define WT_REFRESH_MS 600000UL        // 10 minutes
#endif

// Sliders (see readme): Saturation=speed, FG Bright=intensity, BG Hue=custom1, BG Bright=custom2
static const char _data_FX_MODE_WEATHER[] PROGMEM =
  "Weather@Saturation,FG Bright,BG Hue,BG Bright;;;2;sx=160,ix=220,c1=0,c2=0";

// ---- manually designed per-bar color gradients (edit these to taste) ----
// Each stop is {position 0..255, R, G, B}; stops ascend by position, spanning
// 0..255. A bar's value fraction (0..1) picks an interpolated color along it.
struct WTGradStop { uint8_t pos, r, g, b; };

// Temperature 20..90 F: frost blue -> pale blue -> pale green -> pale yellow -> fiery red
static const WTGradStop GRAD_T[] = {{0,25,80,205},{44,150,200,255},{109,160,230,165},{200,240,235,150},{255,255,65,25}};
// Rain 0..100%: pale warm white -> pale sky white -> deep blue
static const WTGradStop GRAD_P[] = {{0,255,246,222},{102,225,240,255},{255,15,35,190}};
// Tide 0..100% (subtle): pale green -> pale blue -> pale purple
static const WTGradStop GRAD_W[] = {{0,170,225,180},{128,175,205,245},{255,205,185,235}};
// AQI 0..250: pale green -> pale yellow -> medium orange -> fiery red -> purple
static const WTGradStop GRAD_A[] = {{0,170,225,150},{77,240,235,130},{128,245,150,40},{179,240,40,20},{255,150,40,180}};
#define WT_NSTOPS(a) ((int)(sizeof(a)/sizeof(WTGradStop)))

// tide arrow color is by direction (not value): rising=pale blue, falling=pale yellow
static const uint32_t TIDE_ARROW_UP   = RGBW32(180,210,250,0);
static const uint32_t TIDE_ARROW_DOWN = RGBW32(250,240,170,0);
static const uint32_t TIDE_ARROW_FLAT = RGBW32(200,200,200,0);

// sample a gradient at fraction f (0..1)
static uint32_t wtGrad(const WTGradStop* s, int n, float f) {
  if (f <= 0) return RGBW32(s[0].r, s[0].g, s[0].b, 0);
  if (f >= 1) return RGBW32(s[n-1].r, s[n-1].g, s[n-1].b, 0);
  uint8_t p = (uint8_t)(f * 255.0f);
  for (int i = 1; i < n; i++) {
    if (p <= s[i].pos) {
      float span = (float)(s[i].pos - s[i-1].pos); if (span <= 0) span = 1;
      float t = (p - s[i-1].pos) / span;
      uint8_t r = s[i-1].r + (int)(t * (s[i].r - s[i-1].r));
      uint8_t g = s[i-1].g + (int)(t * (s[i].g - s[i-1].g));
      uint8_t b = s[i-1].b + (int)(t * (s[i].b - s[i-1].b));
      return RGBW32(r, g, b, 0);
    }
  }
  return RGBW32(s[n-1].r, s[n-1].g, s[n-1].b, 0);
}

// white-balance gains derived from the CCT ("white balance") slider, per frame
static float wtWbR = 1.0f, wtWbG = 1.0f, wtWbB = 1.0f;

// per-frame foreground controls (from the Saturation / FG Bright sliders)
static float wtSat = 0.63f;   // 0 = white, 1 = full saturation of each hue
static float wtFgB = 0.86f;   // foreground brightness 0..1

static void wtRGB2HSV(uint32_t c, float& h, float& s, float& v) {
  float r=((c>>16)&0xFF)/255.0f, g=((c>>8)&0xFF)/255.0f, b=(c&0xFF)/255.0f;
  float mx = r>g?(r>b?r:b):(g>b?g:b);
  float mn = r<g?(r<b?r:b):(g<b?g:b);
  v = mx; float d = mx - mn;
  s = (mx <= 0) ? 0 : d/mx;
  if (d <= 1e-5f)      h = 0;
  else if (mx == r)    h = fmodf((g-b)/d + 6.0f, 6.0f);
  else if (mx == g)    h = (b-r)/d + 2.0f;
  else                 h = (r-g)/d + 4.0f;
  h /= 6.0f;
}
static uint32_t wtHSV(float h, float s, float v) {   // all 0..1
  float r,g,b;
  int i = (int)(h*6.0f); float f = h*6.0f - i;
  float p = v*(1-s), q = v*(1-f*s), t = v*(1-(1-f)*s);
  switch (((i % 6) + 6) % 6) {
    case 0: r=v;g=t;b=p;break;  case 1: r=q;g=v;b=p;break;
    case 2: r=p;g=v;b=t;break;  case 3: r=p;g=q;b=v;break;
    case 4: r=t;g=p;b=v;break;  default:r=v;g=p;b=q;break;
  }
  return RGBW32((uint8_t)(r*255),(uint8_t)(g*255),(uint8_t)(b*255),0);
}
// re-color a foreground color: keep its hue, take saturation + brightness from the
// sliders. Achromatic sources (grays) stay neutral instead of picking up a hue.
static uint32_t fgColor(uint32_t c) {
  float h,s,v; wtRGB2HSV(c, h, s, v);
  return wtHSV(h, (s < 0.03f) ? 0.0f : wtSat, wtFgB);
}

class WeatherTideUsermod : public Usermod {
  private:
    // ---- design geometry (upright) ----
    static const int N = 20;
    static const int BAR_W = 3;
    static const int BASE_Y = 13;     // bottom row of bars
    static const int BAR_H = 10;      // fill height (rows 4..13)
    static const int ARROW_Y = 0;     // arrows on rows 0..2
    static const int LABEL_Y = 15;    // letters on rows 15..19
    static constexpr int BAR_X[4] = {1, 6, 11, 16};

    static constexpr float TEMP_MIN = 20.0f, TEMP_MAX = 90.0f;
    static constexpr float TIDE_MIN = 0.0f,  TIDE_MAX = 4.2f;
    static constexpr float AQI_MAX  = 250.0f;   // US AQI scaled to full bar

    // ---- cached data (read by the effect) ----
    float    tempNow      = 60.0f;
    int      tempDir      = 0;        // -1 down, 0 flat, +1 up
    float    tempNextFrac = 0.5f;     // temp 24h out, as GRAD_T fraction (arrow color)
    float    rain6        = 0.0f;     // 0..1 chance of rain next 6h
    int      precipDir    = 0;
    float    rain18       = 0.0f;     // 0..1 chance of rain 6-24h (arrow color)
    bool     tideValid    = false;
    float    tideFrac     = 0.5f;
    int      tideDir      = 0;
    float    prevTide     = -1000.0f;
    bool     aqiValid     = false;
    float    aqiFrac      = 0.0f;     // 0..1 of AQI_MAX
    int      aqiDir       = 0;
    float    aqiNextFrac  = 0.0f;     // AQI 24h out, as GRAD_A fraction (arrow color)

    unsigned long lastFetch = 0;
    bool     firstRun = true;
    bool     effectRegistered = false;

    static WeatherTideUsermod* instance;   // for the static effect function

    // -------- small helpers --------
    static int trend(float delta, float dead) {
      if (delta >  dead) return  1;
      if (delta < -dead) return -1;
      return 0;
    }
    // recompute white-balance gains from the CCT ("white balance") slider.
    // cct: 0 = warm (1900K) .. 255 = cool (10091K). Peak channel kept at full
    // so it tints hue without dimming. Applied to every pixel in px().
    static void computeWB() {
      float t = SEGMENT.cct / 255.0f;
      float tr = 255.0f * (1 - t) + 170.0f * t;   // warm boosts red
      float tg = 200.0f * (1 - t) + 215.0f * t;
      float tb = 130.0f * (1 - t) + 255.0f * t;   // cool boosts blue
      float mx = tr; if (tg > mx) mx = tg; if (tb > mx) mx = tb;
      wtWbR = tr / mx; wtWbG = tg / mx; wtWbB = tb / mx;
    }

    // upright design pixel -> panel (90 CCW), with white balance applied.
    static inline void px(int xd, int yd, uint32_t c) {
      uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * wtWbR);
      uint8_t g = (uint8_t)(((c >>  8) & 0xFF) * wtWbG);
      uint8_t b = (uint8_t)(( c        & 0xFF) * wtWbB);
      int rows = SEGMENT.virtualHeight();
      SEGMENT.setPixelColorXY(yd, (rows - 1) - xd, RGBW32(r, g, b, 0));
    }
    static void drawGlyph(int x0, int y0, const uint8_t* rows, int nrows, uint32_t c) {
      for (int gy = 0; gy < nrows; gy++)
        for (int gx = 0; gx < 3; gx++)
          if (rows[gy] & (1 << (2 - gx))) px(x0 + gx, y0 + gy, c);
    }
    static void drawBar(int x0, float frac, uint32_t color) {
      if (frac < 0) frac = 0; if (frac > 1) frac = 1;
      int h = (int)(frac * BAR_H + 0.5f);
      uint8_t tg = (uint8_t)(14 * wtFgB);    // dim neutral track, scaled by FG brightness
      uint32_t track = RGBW32(tg,tg,tg,0);
      for (int x = x0; x < x0 + BAR_W; x++) {
        for (int i = 0; i < BAR_H; i++) px(x, BASE_Y - i, track);
        for (int i = 0; i < h;     i++) px(x, BASE_Y - i, color);
      }
    }

    // -------- the effect --------
    static uint16_t mode_weather() {
      if (!strip.isMatrix || !SEGMENT.is2D()) { SEGMENT.fill(0); return FRAMETIME; }
      WeatherTideUsermod* u = instance;
      if (!u) { SEGMENT.fill(0); return FRAMETIME; }
      computeWB();                                         // white-balance gains
      wtSat = SEGMENT.speed     / 255.0f;                  // Saturation slider
      wtFgB = SEGMENT.intensity / 255.0f;                  // FG Bright slider
      SEGMENT.fill(wtHSV(SEGMENT.custom1 / 255.0f, 1.0f,   // BG Hue + BG Bright
                         SEGMENT.custom2 / 255.0f));

      static const uint8_t GT[5] = {0b111,0b010,0b010,0b010,0b010};
      static const uint8_t GP[5] = {0b111,0b101,0b111,0b100,0b100};
      static const uint8_t GW[5] = {0b101,0b101,0b101,0b111,0b101};
      static const uint8_t GA[5] = {0b010,0b101,0b111,0b101,0b101};
      static const uint8_t A_UP[3]   = {0b010,0b111,0b000};
      static const uint8_t A_DOWN[3] = {0b000,0b111,0b010};
      static const uint8_t A_FLAT[3] = {0b000,0b111,0b000};

      float tf = (u->tempNow - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
      uint32_t tc = fgColor(wtGrad(GRAD_T, WT_NSTOPS(GRAD_T), tf));
      drawBar(BAR_X[0], tf, tc);
      uint32_t pc = fgColor(wtGrad(GRAD_P, WT_NSTOPS(GRAD_P), u->rain6));
      drawBar(BAR_X[1], u->rain6, pc);
      uint32_t wc = u->tideValid ? fgColor(wtGrad(GRAD_W, WT_NSTOPS(GRAD_W), u->tideFrac)) : RGBW32(60,60,60,0);
      drawBar(BAR_X[2], u->tideValid ? u->tideFrac : 0.0f, wc);
      uint32_t qc = u->aqiValid ? fgColor(wtGrad(GRAD_A, WT_NSTOPS(GRAD_A), u->aqiFrac)) : RGBW32(60,60,60,0);
      drawBar(BAR_X[3], u->aqiValid ? u->aqiFrac : 0.0f, qc);

      uint8_t dg = (uint8_t)(40 * wtFgB);
      uint32_t dv = RGBW32(dg,dg,dg,0);
      for (int x = 1; x < N - 1; x++) px(x, 14, dv);

      // arrow shape = trend direction; arrow color = future value (per bar's spec)
      const uint8_t* ta = u->tempDir>0?A_UP:u->tempDir<0?A_DOWN:A_FLAT;
      const uint8_t* pa = u->precipDir>0?A_UP:u->precipDir<0?A_DOWN:A_FLAT;
      int wdir = u->tideValid ? u->tideDir : 0;
      const uint8_t* wa = wdir>0?A_UP:wdir<0?A_DOWN:A_FLAT;
      int qdir = u->aqiValid ? u->aqiDir : 0;
      const uint8_t* qa = qdir>0?A_UP:qdir<0?A_DOWN:A_FLAT;
      uint32_t tac = fgColor(wtGrad(GRAD_T, WT_NSTOPS(GRAD_T), u->tempNextFrac));  // temp +24h
      uint32_t pac = fgColor(wtGrad(GRAD_P, WT_NSTOPS(GRAD_P), u->rain18));        // rain 6-24h
      uint32_t wac = fgColor(wdir>0?TIDE_ARROW_UP:wdir<0?TIDE_ARROW_DOWN:TIDE_ARROW_FLAT);
      uint32_t qac = fgColor(wtGrad(GRAD_A, WT_NSTOPS(GRAD_A), u->aqiNextFrac));   // AQI +24h
      drawGlyph(BAR_X[0], ARROW_Y, ta, 3, tac);
      drawGlyph(BAR_X[1], ARROW_Y, pa, 3, pac);
      drawGlyph(BAR_X[2], ARROW_Y, wa, 3, wac);
      drawGlyph(BAR_X[3], ARROW_Y, qa, 3, qac);

      auto dim = [](uint32_t c)->uint32_t{
        uint8_t r=(c>>16)&0xFF,g=(c>>8)&0xFF,b=c&0xFF;
        return RGBW32((r*9)/10,(g*9)/10,(b*9)/10,0);
      };
      drawGlyph(BAR_X[0], LABEL_Y, GT, 5, dim(tc));
      drawGlyph(BAR_X[1], LABEL_Y, GP, 5, dim(pc));
      drawGlyph(BAR_X[2], LABEL_Y, GW, 5, dim(wc));
      drawGlyph(BAR_X[3], LABEL_Y, GA, 5, dim(qc));
      return FRAMETIME;
    }

    // -------- data fetch (manual HTTP/1.0, mirrors in-tree klipper usermod) --------
    bool doReq(Client& c, const char* host, uint16_t port, const String& path,
               std::function<bool(Stream&)> parse) {
      c.setTimeout(5000);
      if (!c.connect(host, port)) { DEBUG_PRINTLN(F("[WT] connect failed")); return false; }
      c.print(F("GET ")); c.print(path); c.print(F(" HTTP/1.0\r\nHost: "));
      c.print(host);
      c.print(F("\r\nUser-Agent: wled\r\nConnection: close\r\n\r\n"));
      bool ok = false;
      if (c.find((char*)"\r\n\r\n")) ok = parse(c);   // skip headers
      c.stop();
      return ok;
    }
    bool fetchJson(bool secure, const char* host, const String& path,
                   std::function<bool(Stream&)> parse) {
      if (secure) {
        WiFiClientSecure c; c.setInsecure();
        return doReq(c, host, 443, path, parse);
      } else {
        WiFiClient c;
        return doReq(c, host, 80, path, parse);
      }
    }

    void fetchWeather() {
      char path[224];
      snprintf(path, sizeof(path),
        "/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m&hourly=temperature_2m,precipitation_probability"
        "&forecast_hours=25&temperature_unit=fahrenheit&timezone=America%%2FNew_York",
        (double)WT_LAT, (double)WT_LON);

      StaticJsonDocument<192> filter;
      filter["current"]["temperature_2m"] = true;
      filter["hourly"]["temperature_2m"] = true;
      filter["hourly"]["precipitation_probability"] = true;

      fetchJson(false, "api.open-meteo.com", String(path), [&](Stream& s) -> bool {
        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, s, DeserializationOption::Filter(filter));
        if (err) { DEBUG_PRINTF("[WT] weather json err: %s\n", err.c_str()); return false; }
        tempNow = doc["current"]["temperature_2m"] | tempNow;
        JsonArray temps = doc["hourly"]["temperature_2m"];
        JsonArray probs = doc["hourly"]["precipitation_probability"];
        if (temps.size() < 25 || probs.size() < 25) return false;
        float p6 = 0, p18 = 0;
        for (int i = 1; i <= 6;  i++)  p6  += (float)(probs[i] | 0);
        for (int i = 7; i <= 24; i++)  p18 += (float)(probs[i] | 0);
        p6 /= 6.0f; p18 /= 18.0f;
        rain6 = p6 / 100.0f;                     // chance of rain next 6h
        rain18 = p18 / 100.0f;                    // chance of rain 6-24h (arrow color)
        precipDir = trend(p18 - p6, 10.0f);      // up = more rain in the following 18h
        float t24 = temps[24] | tempNow;
        tempDir = trend(t24 - tempNow, 1.0f);
        tempNextFrac = (t24 - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
        return true;
      });
    }

    void fetchAirQuality() {
      char path[192];
      snprintf(path, sizeof(path),
        "/v1/air-quality?latitude=%.4f&longitude=%.4f"
        "&current=us_aqi&hourly=us_aqi&forecast_hours=25&timezone=America%%2FNew_York",
        (double)WT_LAT, (double)WT_LON);

      StaticJsonDocument<128> filter;
      filter["current"]["us_aqi"] = true;
      filter["hourly"]["us_aqi"] = true;

      fetchJson(false, "air-quality-api.open-meteo.com", String(path), [&](Stream& s) -> bool {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, s, DeserializationOption::Filter(filter));
        if (err) { DEBUG_PRINTF("[WT] aqi json err: %s\n", err.c_str()); return false; }
        float aqi = doc["current"]["us_aqi"] | -1.0f;
        if (aqi < 0) return false;
        aqiFrac = aqi / AQI_MAX;
        if (aqiFrac < 0) aqiFrac = 0; if (aqiFrac > 1) aqiFrac = 1;
        JsonArray aq = doc["hourly"]["us_aqi"];
        if (aq.size() >= 25) {
          float a24 = (float)(aq[24] | (int)aqi);
          aqiDir = trend(a24 - aqi, 10.0f);
          aqiNextFrac = a24 / AQI_MAX;            // arrow color = AQI 24h out
        }
        aqiValid = true;
        return true;
      });
    }

    void fetchTide() {
      String path = String("/api/prod/datagetter?date=latest&station=") + WT_TIDE_STATION +
                    "&product=water_level&datum=MLLW&time_zone=lst_ldt&units=english"
                    "&application=wled_ledwall&format=json";
      fetchJson(true, "api.tidesandcurrents.noaa.gov", path, [&](Stream& s) -> bool {
        DynamicJsonDocument doc(1024);
        DeserializationError err = deserializeJson(doc, s);
        if (err) { DEBUG_PRINTF("[WT] tide json err: %s\n", err.c_str()); return false; }
        JsonArray data = doc["data"];
        if (data.isNull() || data.size() == 0) return false;
        const char* v = data[0]["v"];
        if (!v) return false;
        float level = atof(v);
        tideFrac = (level - TIDE_MIN) / (TIDE_MAX - TIDE_MIN);
        if (tideFrac < 0) tideFrac = 0; if (tideFrac > 1) tideFrac = 1;
        if (prevTide > -999.0f) tideDir = trend(level - prevTide, 0.03f);
        prevTide = level;
        tideValid = true;
        return true;
      });
    }

  public:
    WeatherTideUsermod() { instance = this; }

    void setup() override {
      if (!effectRegistered) {
        strip.addEffect(255, &WeatherTideUsermod::mode_weather, _data_FX_MODE_WEATHER);
        effectRegistered = true;
      }
    }

    void loop() override {
      if (!WLED_CONNECTED) return;
      unsigned long now = millis();
      if (firstRun || now - lastFetch >= WT_REFRESH_MS) {
        if (firstRun && now < 8000) return;   // let WiFi/NTP settle
        firstRun = false;
        lastFetch = now;
        fetchWeather();
        fetchTide();
        fetchAirQuality();
        DEBUG_PRINTF("[WT] temp=%.1f(%d) rain6=%.2f(%d) tideFrac=%.2f(%d) aqiFrac=%.2f(%d)\n",
                     tempNow, tempDir, rain6, precipDir, tideFrac, tideDir, aqiFrac, aqiDir);
      }
    }

    uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

WeatherTideUsermod* WeatherTideUsermod::instance = nullptr;
constexpr int WeatherTideUsermod::BAR_X[4];
