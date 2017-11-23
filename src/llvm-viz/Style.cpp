//
// Created by fader on 03.03.16.
//

#include "Style.hpp"
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Format.h>

using namespace html;
using namespace llvm;

std::string ColorRGB::css() const {
  SmallString<32> str;
  raw_svector_ostream OS{str};

  OS << "rgb(" << format_decimal(R, 3) << ", " << format_decimal(G, 3) << ", " << format_decimal(B, 3) << ")";

  return str.str().str();
}

std::string ColorRGB::dot() const {
  SmallString<32> str;
  raw_svector_ostream OS{str};

  OS << "#(" << format_hex(R, 2) << ", " << format_hex(G, 2) << ", " << format_hex(B, 2) << ")";

  return str.str().str();
}

unsigned Style::maxLoopDepth() { return 7; }

ColorRGB Style::hardColorForLoopDepth(unsigned depth) {
  switch (depth) {
    case 0:  return ColorRGB{255, 255, 255};
    case 1:  return ColorRGB{255, 240, 220};
    case 2:  return ColorRGB{239, 198, 182};
    case 3:  return ColorRGB{223, 156, 144};
    case 4:  return ColorRGB{207, 114, 106};
    case 5:  return ColorRGB{191,  71,  68};
    case 6:  return ColorRGB{175,  30,  30};
    default: return ColorRGB{150, 200, 255};
  }
}
ColorRGB Style::softColorForLoopDepth(unsigned depth) {
  switch (depth) {
    case 0:  return ColorRGB{220, 220, 220};
    case 1:  return ColorRGB{255, 200, 140};
    case 2:  return ColorRGB{234, 160, 112};
    case 3:  return ColorRGB{213, 120,  84};
    case 4:  return ColorRGB{192,  80,  56};
    case 5:  return ColorRGB{171,  39,  27};
    case 6:  return ColorRGB{150,   0,   0};
    default: return ColorRGB{100, 100, 255};
  }
}
