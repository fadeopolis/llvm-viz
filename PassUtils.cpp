//
// Created by fader on 28.02.16.
//

#include "PassUtils.hpp"
#include "HtmlUtils.hpp"
#include <llvm/PassRegistry.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>

using namespace html;
using namespace llvm;

std::string InstructionNamer::getId(const Value *v) {
  assert(v);

  auto it = _ids.find(v);

  if (it != _ids.end()) {
    return it->second;
  }

  std::string str;
  raw_string_ostream OS{str};

  if (isa<Constant>(v)) {
    v->printAsOperand(OS, false, _slots);
  } else if (auto bb = dyn_cast<BasicBlock>(v)) {
    OS << bb->getParent()->getName();
    OS << '.';
    v->printAsOperand(OS, false, _slots);
  } else if (auto inst = dyn_cast<Instruction>(v)) {
    OS << inst->getFunction()->getName();
    OS << '.';
    v->printAsOperand(OS, false, _slots);
  } else if (auto arg = dyn_cast<Argument>(v)) {
    OS << arg->getParent()->getName();
    OS << '.';
    v->printAsOperand(OS, false, _slots);
  } else {
    llvm_unreachable("Invalid Value kind");
  }

  OS.flush();

  auto new_it = _ids.insert(std::make_pair(v, str));

  return new_it.first->second;
}

std::string InstructionNamer::asOperand(const Value &v) {
  std::string str;
  raw_string_ostream OS{str};

  v.printAsOperand(OS, false, _slots);
  OS.flush();

  return str;
}

Html* InstructionNamer::ref(const Value* v) {
  if (auto glbl = dyn_cast<GlobalValue>(v)) {
    if (glbl->isDeclaration()) {
      return makeString(v);
    } else {
      return makeLink(v);
    }
  } else if (isa<Constant>(v)) {
    return makeString(v);
  } else {
    return makeLink(v);
  }
}

Html* InstructionNamer::makeLink(const Value *v) {
  return tag("a", attr("href", '#' + getId(v)), html::str(asOperand(v)));
}

Html* InstructionNamer::makeString(const Value *v) {
  return html::str(getId(v));
}
