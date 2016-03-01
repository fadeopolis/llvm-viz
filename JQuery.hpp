//
// Created by fader on 28.02.16.
//

#pragma once

#include <llvm/ADT/StringRef.h>

namespace html {

using namespace llvm;

/// Returns the source code of jQuery as a string.
extern StringRef jQuerySource();

} // end namespace html