//
// Created by fader on 28.02.16.
//

#include "ValueNameMangler.hpp"
#include "HtmlUtils.hpp"
#include <llvm/PassRegistry.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <support/PrintUtils.hpp>

using namespace html;
using namespace llvm;

std::string ValueNameMangler::getId(const Value *v) {
  assert(v);

  assert(!_ids.count(v) || !_ids[v].empty());
  auto& id = _ids[v];

  if (id.empty()) {
    raw_string_ostream OS{id};
    getId(v, OS);
    OS.flush();
  }

  return id;
}

void ValueNameMangler::getId(const Value *v, raw_ostream &OS) {
  assert(v);
  assert(!isa<Constant>(v) || isa<GlobalValue>(v));

  auto isHex = [](char c) -> bool {
    return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
  };
  (void) isHex;

  /// print ID to temporary string
  SmallString<32> buf;
  {
    raw_svector_ostream OS{buf};
    if (isa<GlobalValue>(v)) {
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
  }

  /// escape special chars
  for (size_t i = 0, e = buf.size(); i < e; i++) {
    char c = buf[i];

    /// Escape all special chars in identifier.
    /// See: http://llvm.org/docs/LangRef.html#identifiers
    switch (c) {
      case '_':
        OS << "___";
        break;
      case '@':
        OS << "_at_";
        break;
      case '%':
        OS << "_percent_";
        break;
      case '.':
        OS << "_dot_";
        break;
      case '-':
        OS << "_dash_";
        break;
      case '$':
        OS << "_dollar_";
        break;
      case '"':
        OS << "_quote_";
        break;
        /// llvm escape in identifier '\xx'
      case '\\': {
        assert((i + 2) < buf.size() && "Malformed escape sequence in LLVM identifier");
        char a = buf[i + 1];
        char b = buf[i + 2];
        assert(isHex(a));
        assert(isHex(b));

        OS << "_x" << a << b << '_';
        i += 2;
        break;
      }
      default:
        assert(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')));
        OS << c;
        break;
    }
  }
}

void ValueNameMangler::asOperand(const Value *v, raw_ostream& OS) {
  assert(v);
  v->printAsOperand(OS, false, _slots);
}

std::string ValueNameMangler::asOperand(const Value &v) {
  std::string str;
  raw_string_ostream OS{str};

  asOperand(v, OS);
  OS.flush();

  return str;
}

Html* ValueNameMangler::ref(const Value* v) {
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

Html* ValueNameMangler::makeLink(const Value *v) {
  return tag(
    "a",
    attr("href",  '#' + getId(v)),
    /// add type of value as mouseover text
    attr("title", print(*v->getType(), false)),
    asOperand(v)
  );
}

Html* ValueNameMangler::makeString(const Value *v) {
  return html::span(
    /// add type of value as mouseover text
    attr("title", print(*v->getType(), false)),
    asOperand(v)
  );
}
