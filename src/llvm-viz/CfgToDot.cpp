//
// Created by fader on 02.03.16.
//

#include "CfgToDot.hpp"
#include "ValueNameMangler.hpp"
#include <llvm/IR/Function.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Support/raw_ostream.h>

using namespace html;
using namespace llvm;

static bool isHex(char c) {
  return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
}

struct escape_name {
  escape_name(ValueNameMangler& names, const Value *v) : names{names}, v{v} { }
  escape_name(ValueNameMangler& names, const Value &v) : names{names}, v{&v} { }

  friend raw_ostream &operator<<(raw_ostream &OS, escape_name en) {
    en.names.getId(en.v, OS);
    return OS;
  }
private:
  ValueNameMangler& names;
  const Value* v;
};

struct as_operand {
  as_operand(ValueNameMangler& names, const Value *v) : names{names}, v{v} { }
  as_operand(ValueNameMangler& names, const Value &v) : names{names}, v{&v} { }

  friend raw_ostream &operator<<(raw_ostream &OS, as_operand en) {
    en.names.asOperand(en.v, OS);
    return OS;
  }
private:
  ValueNameMangler& names;
  const Value* v;
};

struct CfgDumper {
  CfgDumper(raw_ostream& OS, ValueNameMangler& names, const LoopInfo& loops)
  : OS{OS}
  , _names{names}
  , _loops{loops}
  {}

  void printFunction(const Function& fn) {
    OS << "digraph {\n";
    OS << "\n";

    for (auto* loop : _loops) {
      printLoop(loop);
    }

    OS << "\n";

    for (auto& bb : fn) {
      if (!_loops.getLoopFor(&bb)) {
        printBB(&bb, 2);
      }
    }

    OS << "\n";

    for (auto& bb : fn) {
      for (auto succ : successors(&bb)) {
        OS.indent(2) << escape_name{_names, bb} << " -> " << escape_name{_names, succ} << ";\n";
      }
    }
    OS << "}\n";
  }

  void printLoop(const Loop* loop) {
    unsigned lvl = 2 * loop->getLoopDepth();

    OS.indent(lvl) << "subgraph " << genLoopId() << " {\n";
    OS.indent(lvl) << "  style=invis; // remove box around subgraphs\n";
    lvl += 2;
    for (const auto* bb : loop->getBlocks()) {
      printBB(bb, lvl);
    }

    for (auto* subLoop : loop->getSubLoops())
      printLoop(subLoop);

    lvl -= 2;
    OS.indent(lvl) << "}\n";
  }

  void printBB(const BasicBlock* bb, unsigned lvl) {
//  OS.indent(lvl) << escape_name{names, bb} << "[label=\"" << as_operand{names, bb} << "\"];\n";

    OS.indent(lvl);
    OS << escape_name{_names, bb};
    OS << "[label=\"" << as_operand{_names, bb} << "\"]";
    OS << "[href=\"" << '#' << _names.getId(bb) << "\"]";
    OS << "[shape=box]\n";

    if (auto loop = _loops.getLoopFor(bb)) {
      if (loop->getHeader() == bb) {
        OS << "[style=rounded]\n";
      }
    }

    OS << ";\n";
  }

  std::string genLoopId() {
    std::string out = "cluster_" + std::to_string(id_gen);
    id_gen++;
    return out;
  }
private:
  unsigned id_gen;
  raw_ostream&      OS;
  ValueNameMangler& _names;
  const LoopInfo&   _loops;
};

void html::cfg2dot(raw_ostream& OS, const Function& fn, ValueNameMangler& names, const LoopInfo& loops) {
  CfgDumper cfg{OS, names, loops};
  cfg.printFunction(fn);
}


