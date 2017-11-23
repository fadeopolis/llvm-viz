//
// Created by fader on 28.02.16.
//

#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>

/// Helper class for printing comma separated lists and the like.
/// It prints something different the first time it is printed (usually just an empty string).
/// Use like so:
/// @code
///   Separator sep;
///   for (auto elem : seq)
///     outs() << sep << elem;
/// @endcode
struct Separator {
  explicit Separator(llvm::StringRef sep = ", ") : Separator{"", sep} { }

  Separator(llvm::StringRef first, llvm::StringRef sep) : _first{first}, _sep{sep} { }

  llvm::StringRef str() {
    if (_printed) {
      return _sep;
    } else {
      _printed = true;
      return _first;
    }
  }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, Separator &sep) {
    return OS << sep.str();
  }
private:
  const llvm::StringRef _first, _sep;
  bool _printed = false;
};

/// Simple wrapper class useful for printing long string literals.
/// When printing it skips the common leading spaces on all lines.
/// Leading and trailing empty lines are also not printed.
struct LongStringLiteral {
  explicit constexpr LongStringLiteral(llvm::StringRef str) : _str{str} {}

  llvm::StringRef str() const { return _str; }

  /// Print to stream adding a given indent before each line after having removed the common leading whitespace.
  void print(llvm::raw_ostream& OS, unsigned indent = 0) const;

  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& OS, LongStringLiteral str) {
    str.print(OS, 0);
    return OS;
  }
private:
  const llvm::StringRef _str;
};

/***
 * Prints current time in human readable format to a stream
 */
struct TimeStamp final {
  constexpr TimeStamp() {}

  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& OS, TimeStamp) {
    time_t    now     = time(0);
    struct tm tstruct = *localtime(&now);

    char buf[128];
    strftime(buf, sizeof(buf), "%x (%X)", &tstruct);

    return OS << buf;
  }
};

/// helper for printing current time to stream.
static TimeStamp timestamp;

/***
 * Calls member function print() on @p p given arguments @args
 */
template<typename P, typename... Args>
std::string print(const P& p, Args&&... args) {
  std::string buf;
  llvm::raw_string_ostream OS{buf};

  p.print(OS, std::forward<Args>(args)...);
  OS.flush();

  return buf;
};

/***
 * Calls member function printAsOperand() on @p p given arguments @args
 */
template<typename P, typename... Args>
std::string printAsOperand(const P& p, Args&&... args) {
  std::string buf;
  llvm::raw_string_ostream OS{buf};

  p.printAsOperand(OS, std::forward<Args>(args)...);
  OS.flush();

  return buf;
};
