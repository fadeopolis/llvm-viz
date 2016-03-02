//
// Created by fader on 24.02.16.
//

#include "HtmlUtils.hpp"
#include "PrintUtils.hpp"
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>

using namespace llvm;
using namespace html;

static void indent(unsigned indent_lvl, raw_ostream& OS);
static void nl(unsigned indent_lvl, raw_ostream& OS);
static unsigned inc_indent(unsigned indent_lvl);

void html::HtmlEscapedString::_print(raw_ostream &OS, unsigned indent_lvl) const {
  indent(indent_lvl, OS);

  print_str(OS, _txt);

  nl(indent_lvl, OS);
}
void html::HtmlEntity::_print(raw_ostream &OS, unsigned indent_lvl) const {
  indent(indent_lvl, OS);

  OS << str();

  nl(indent_lvl, OS);
}

StringRef html::HtmlEntity::str() const {
  switch (_entity) {
    case TIMES: return "&times;";
    case MINUS: return "&minus;";
    default: llvm_unreachable("Garbage HTML entity");
  }
}

template<typename Attrs>
static void print_attrs(raw_ostream& OS, const Attrs& attrs) {
  for (const auto& attr : attrs)
    OS << ' ' << attr.name() << "=\"" << attr.value() << '"';
}

void SimpleTag::_print(raw_ostream &OS, unsigned indent_lvl) const {
  if (style() == FlowStyle)
    indent_lvl = FLOW_STYLE;

  indent(indent_lvl, OS);

  OS << '<' << _tag;
  print_attrs(OS, _attrs);
  OS << '>';
  nl(indent_lvl, OS);

  for (auto html : _body)
    html->print(OS, inc_indent(indent_lvl));

  indent(indent_lvl, OS);
  print_close(OS, _tag);

  nl(indent_lvl, OS);
}

void EmptyTag::_print(raw_ostream &OS, unsigned indent_lvl) const {
  indent(indent_lvl, OS);

  OS << '<' << tag();
  print_attrs(OS, attrs());
  OS << '>';

  nl(indent_lvl, OS);
}

void VerbatimTag::_print(raw_ostream &OS, unsigned indent_lvl) const {
  indent(indent_lvl, OS);

  OS << '<' << _tag;
  print_attrs(OS, _attrs);
  OS << '>';
  nl(indent_lvl, OS);

  LongStringLiteral{_body}.print(OS, indent_lvl + 2);

  indent(indent_lvl, OS);
  print_close(OS, _tag);

  nl(indent_lvl, OS);
}


void html::print_str(raw_ostream& OS, StringRef str) {
  for (char c : str) {
    assert(isprint(c));

    switch (c) {
      case '&': OS << "&amp;"; break;
      case '<': OS << "&lt;"; break;
      case '>': OS << "&gt;"; break;
      case '"': OS << "&quot;"; break;
      default:  OS << c; break;
    }
  }
}

void html::print_empty(raw_ostream& OS, const Twine& tag) {
  OS << '<' << tag << "/>";
}
void html::print_empty(raw_ostream &OS, const Twine& tag, const HtmlAttrs &attrs) {
  OS << '<' << tag;
  print_attrs(OS, attrs);
  OS << "/>";
}


void html::print_open(raw_ostream& OS, const Twine& tag) {
  OS << '<' << tag << '>';
}
void html::print_open(raw_ostream& OS, const Twine& tag, const HtmlAttrs& attrs) {
  OS << '<' << tag;
  print_attrs(OS, attrs);
  OS << '>';
}

void html::print_close(raw_ostream& OS, const Twine& tag) {
  OS << "</" << tag << '>';
}


static void indent(unsigned indent_lvl, raw_ostream &OS) {
  if (indent_lvl != Html::FLOW_STYLE)
    OS.indent(indent_lvl);
}
static void nl(unsigned indent_lvl, raw_ostream &OS) {
  if (indent_lvl != Html::FLOW_STYLE)
    OS << '\n';
}
static unsigned inc_indent(unsigned indent_lvl) {
  return (indent_lvl == Html::FLOW_STYLE) ? Html::FLOW_STYLE : (indent_lvl + 2);
}
