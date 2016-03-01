//
// Created by fader on 28.02.16.
//

#pragma once

#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/IR/ValueMap.h>
#include "Utils.hpp"

namespace html {

struct Html;
struct HtmlTag;

using namespace llvm;

struct InstructionNamer {
  InstructionNamer(ModuleSlotTracker& slots) : _slots{slots} {}

  std::string getId(const Value* v);
  std::string getId(const Value& v) { return getId(&v); }

  std::string asOperand(const Value& v);
  std::string asOperand(const Value* v) {
    assert(v);
    return asOperand(*v);
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
