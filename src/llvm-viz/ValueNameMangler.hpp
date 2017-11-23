//
// Created by fader on 28.02.16.
//

#pragma once

#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/IR/ValueMap.h>

namespace html {

struct Html;
struct HtmlTag;

using namespace llvm;

/***
 * Mangles names of LLVM values so they are usable as IDs in CSS.
 */
struct ValueNameMangler {
  ValueNameMangler(ModuleSlotTracker& slots) : _slots{slots} {}

  /// Returns a unique ID (at module scope) for a given value that can be used in CSS.
  /// All symbols except [a-zA-Z0-9_] are escaped, and the final string only contains those chars.
  std::string getId(const Value* v);
  std::string getId(const Value& v) { return getId(&v); }

  void getId(const Value& v, raw_ostream& OS) {
    getId(&v, OS);
  }
  void getId(const Value* v, raw_ostream& OS);

  std::string asOperand(const Value& v);
  std::string asOperand(const Value* v) {
    assert(v);
    return asOperand(*v);
  }

  void asOperand(const Value* v, raw_ostream& OS);
  void asOperand(const Value& v, raw_ostream& OS) {
    asOperand(&v, OS);
  }

  /// Creates a <a href="#..."> tag or simple string for the given value.
  /// Constants and external globals are rendered as a simple string, for other values a link is created.
  Html* ref(const Value* v);
  Html* ref(const Value& v) {
    return ref(&v);
  }
private:
  Html* makeLink(const Value* v);
  Html* makeString(const Value* v);

  ModuleSlotTracker& _slots;
  ValueMap<const Value*, std::string> _ids;
};

} // end namespace html
