//
// Created by fader on 03.03.16.
//

#pragma once

#include <string>

namespace html {

struct ColorRGB {
  ColorRGB() : ColorRGB{0,0,0} {}
  ColorRGB(unsigned char R, unsigned char G, unsigned char B) : R{R}, G{G}, B{B} {}

  const unsigned char R, G, B;

  std::string css() const;
  std::string dot() const;
};

struct Style {
  static unsigned maxLoopDepth();
  static ColorRGB hardColorForLoopDepth(unsigned depth);
  static ColorRGB softColorForLoopDepth(unsigned depth);
};

} // end namespace html
