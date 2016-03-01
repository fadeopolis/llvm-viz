//
// Created by fader on 28.02.16.
//

#include "PrintUtils.hpp"
#include <llvm/ADT/SmallVector.h>

using namespace html;
using namespace llvm;

void LongStringLiteral::print(raw_ostream &OS, unsigned int indent) {
  StringRef str = this->str();
//
//  // TODO: make the line splitting loop smarter to remove this condition.
//  assert(str.back() == '\n');

  size_t first_line = 0, last_line = 0;

  /// split str into vector of lines and count common leading spaces
  SmallVector<StringRef, 4> lines;
  size_t num_common_spaces = -1;

  {
    StringRef::size_type pos = 0;
    StringRef::size_type prev = 0;

    size_t lineno = 0;

    while ((pos = str.find('\n', prev)) != StringRef::npos) {
      auto line = str.substr(prev, pos - prev);
      lines.push_back(line);

      if (!line.empty()) {
        size_t num_leading_spaces = 0;
        for (char c : line) {
          if (c != ' ')
            break;
          num_leading_spaces++;
        }
        num_common_spaces = std::min(num_common_spaces, num_leading_spaces);

        if (!first_line)
          first_line = lineno;

        last_line = lineno;
      }

      prev = pos + 1;
      lineno++;
    }
  }

  /// print lines without common leading spaces
  for (size_t i = first_line, end = last_line; i <= end; i++) {
    auto line = lines[i];

    OS.indent(indent) << line.substr(num_common_spaces) << '\n';
  }
}
