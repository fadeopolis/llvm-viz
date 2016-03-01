//
// Created by fader on 28.02.16.
//

#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>

namespace html {

using namespace llvm;

/// Helper class for printing comma separated lists and the like.
/// It prints something different the first time it is printed (usually just an empty string).
/// Use like so:
/// @code
///   Separator sep;
///   for (auto elem : seq)
///     outs() << sep << elem;
/// @endcode
struct Separator {
  explicit Separator(StringRef sep = ", ") : Separator{"", sep} { }

  Separator(StringRef first, StringRef sep) : _first{first}, _sep{sep} { }

  StringRef str() {
    if (_printed) {
      return _sep;
    } else {
      _printed = true;
      return _first;
    }
  }

  friend raw_ostream &operator<<(raw_ostream &OS, Separator &sep) {
    return OS << sep.str();
  }
private:
  StringRef _first, _sep;
  bool _printed = false;
};

/// Simple wrapper class useful for printing long string literals.
/// When printing it skips the common leading spaces on all lines.
/// Leading and trailing empty lines are also not printed.
struct LongStringLiteral {
  explicit LongStringLiteral(StringRef str) : _str{str} {}

  StringRef str() const { return _str; }

  /// Print to stream adding a given indent before each line after having removed the common leading whitespace.
  void print(raw_ostream& OS, unsigned indent = 0);

  friend raw_ostream& operator<<(raw_ostream& OS, LongStringLiteral str) {
    str.print(OS, 0);
    return OS;
  }
private:
  StringRef _str;
};

template<typename P, typename... Args>
std::string print(const P& p, Args&&... args) {
  std::string buf;
  raw_string_ostream OS{buf};

  p.print(OS, std::forward<Args>(args)...);
  OS.flush();

  return buf;
};

template<typename P, typename... Args>
std::string printAsOperand(const P& p, Args&&... args) {
  std::string buf;
  raw_string_ostream OS{buf};

  p.printAsOperand(OS, std::forward<Args>(args)...);
  OS.flush();

  return buf;
};

} // end namespace html
