#pragma once
#include "wled.h"

/*
 * Cylinder-grid pattern, implemented as a selectable WLED 2D effect ("Grid").
 *
 * Renders the exact 20x20 color pattern captured from the project's index.html
 * cylinder sculpture (separable sinusoidal field, 5 height steps). The grid is
 * hardcoded below as a table of level indices 0..4 (0 = shortest, 4 = tallest),
 * each mapped to a fixed color short->blue ... long->red (matching index.html's
 * HSL length->color ramp).
 *
 * Design coords are upright (x=0 left, y=0 top) then mapped to the panel with a
 * 90-degrees-CCW rotation to match this wall's wiring (same convention as the
 * weather usermod).
 *
 * Sliders: FG Bright = intensity (overall brightness), Saturation = speed.
 */

// Sliders: Saturation=speed, FG Bright=intensity
static const char _data_FX_MODE_GRID[] PROGMEM =
  "Grid@Saturation,FG Bright;;;2;sx=217,ix=255";

// 5 level colors, blue (shortest) -> red (tallest), from index.html's HSL ramp
// (hue 0.65..0.0, sat 0.85, light 0.5).
static const uint32_t CG_LEVEL_COLOR[5] = {
  RGBW32( 19,  41, 236, 0),   // 0  blue
  RGBW32( 19, 236, 220, 0),   // 1  cyan
  RGBW32( 30, 236,  19, 0),   // 2  green
  RGBW32(236, 230,  19, 0),   // 3  yellow
  RGBW32(236,  19,  19, 0),   // 4  red
};

// exact level grid [y][x], y=0 top / x=0 left (upright design coords)
static const uint8_t CG_GRID[20][20] = {
  {4,4,3,3,2,2,2,3,4,4,4,4,3,2,2,2,3,3,4,4},
  {4,4,3,2,2,2,2,3,3,4,4,3,3,2,2,2,2,3,4,4},
  {3,3,2,2,1,1,2,2,3,3,3,3,2,2,1,1,2,2,3,3},
  {3,2,2,1,1,1,1,2,2,3,3,2,2,1,1,1,1,2,2,3},
  {2,2,1,1,0,0,0,1,2,2,2,2,1,0,0,0,1,1,2,2},
  {2,2,1,1,0,0,0,1,2,2,2,2,1,0,0,0,1,1,2,2},
  {2,2,2,1,0,0,1,1,2,2,2,2,1,1,0,0,1,2,2,2},
  {3,3,2,2,1,1,1,2,2,3,3,2,2,1,1,1,2,2,3,3},
  {4,3,3,2,2,2,2,2,3,3,3,3,2,2,2,2,2,3,3,4},
  {4,4,3,3,2,2,2,3,3,4,4,3,3,2,2,2,3,3,4,4},
  {4,4,3,3,2,2,2,3,3,4,4,3,3,2,2,2,3,3,4,4},
  {4,3,3,2,2,2,2,2,3,3,3,3,2,2,2,2,2,3,3,4},
  {3,3,2,2,1,1,1,2,2,3,3,2,2,1,1,1,2,2,3,3},
  {2,2,2,1,0,0,1,1,2,2,2,2,1,1,0,0,1,2,2,2},
  {2,2,1,1,0,0,0,1,2,2,2,2,1,0,0,0,1,1,2,2},
  {2,2,1,1,0,0,0,1,2,2,2,2,1,0,0,0,1,1,2,2},
  {3,2,2,1,1,1,1,2,2,3,3,2,2,1,1,1,1,2,2,3},
  {3,3,2,2,1,1,2,2,3,3,3,3,2,2,1,1,2,2,3,3},
  {4,4,3,2,2,2,2,3,3,4,4,3,3,2,2,2,2,3,4,4},
  {4,4,3,3,2,2,2,3,4,4,4,4,3,2,2,2,3,3,4,4},
};

class CylinderGridUsermod : public Usermod {
  private:
    static const int N = 20;
    static CylinderGridUsermod* instance;
    bool effectRegistered = false;

    // -------- the effect --------
    static uint16_t mode_grid() {
      if (!strip.isMatrix || !SEGMENT.is2D()) { SEGMENT.fill(0); return FRAMETIME; }
      float sat = SEGMENT.speed     / 255.0f;   // Saturation slider (1 = as designed)
      float bri = SEGMENT.intensity / 255.0f;   // FG Bright slider
      int rows = SEGMENT.virtualHeight();
      for (int yd = 0; yd < N; yd++)
        for (int xd = 0; xd < N; xd++) {
          uint32_t c = CG_LEVEL_COLOR[CG_GRID[yd][xd]];
          uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
          if (sat < 1.0f) {   // desaturate toward luma
            uint8_t y = (uint8_t)(0.299f*r + 0.587f*g + 0.114f*b);
            r = (uint8_t)(y + (r - y) * sat);
            g = (uint8_t)(y + (g - y) * sat);
            b = (uint8_t)(y + (b - y) * sat);
          }
          r = (uint8_t)(r * bri); g = (uint8_t)(g * bri); b = (uint8_t)(b * bri);
          SEGMENT.setPixelColorXY(yd, (rows - 1) - xd, RGBW32(r, g, b, 0));  // 90 CCW
        }
      return FRAMETIME;
    }

  public:
    CylinderGridUsermod() { instance = this; }

    void setup() override {
      if (!effectRegistered) {
        strip.addEffect(254, &CylinderGridUsermod::mode_grid, _data_FX_MODE_GRID);
        effectRegistered = true;
      }
    }

    void loop() override {}   // static pattern: nothing to do per-frame

    uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

CylinderGridUsermod* CylinderGridUsermod::instance = nullptr;
