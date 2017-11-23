//
// Created by fader on 02.03.16.
//

#pragma once

namespace llvm {
  class Function;
  class LoopInfo;
  class raw_ostream;
}

namespace html {

using namespace llvm;

struct ValueNameMangler;

/***
 * write CFG of BBs in a function to a stream in graphviz .dot format.
 */
void cfg2dot(raw_ostream& OS, const Function& fn, ValueNameMangler& names, const LoopInfo& loops);

} // end namespace html
