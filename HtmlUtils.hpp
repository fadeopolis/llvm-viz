//
// Created by fader on 24.02.16.
//

#pragma once

#include <llvm/ADT/Twine.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/Optional.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

namespace llvm {
class Value;
class Type;
class ModuleSlotTracker;
}

//namespace html {
//
//using namespace llvm;
//
//struct HtmlEscape {
//  explicit HtmlEscape(const Twine& twine) : _twine{twine} {}
//
//  SmallString<24> str() const {
//    SmallString<24> buf;
//    raw_svector_ostream OS{buf};
//
//    OS << *this;
//
//    return buf;
//  }
//
//  friend raw_ostream& operator<<(raw_ostream& OS, const HtmlEscape& esc);
//
//  const Twine& _twine;
//};
//
//struct HtmlTag {
//  enum Kind {
//    Open, Close, Empty
//  };
//
//  explicit HtmlTag(const Twine& tag, const Twine& attrs, Kind kind) : _tag{tag}, _attrs{attrs}, _kind{kind} {}
//
//  friend raw_ostream& operator<<(raw_ostream& OS, const HtmlTag& esc);
//
//  const Twine& _tag, _attrs;
//  const Kind _kind;
//};
//
///// Helper for printing simple, non-nested, html tags without attributes.
//struct SimpleHtml {
//  explicit SimpleHtml(const Twine& tag, const Twine& attrs, const Twine& content) : _tag{tag}, _attrs{attrs}, _content{content} {}
//
//  friend raw_ostream& operator<<(raw_ostream& OS, const SimpleHtml& esc);
//
//  const Twine& _tag, _attrs, _content;
//};
//
//struct Separator {
//  explicit Separator(const Twine& sep) : Separator{"", sep} {}
//
//  Separator(const Twine& first, const Twine& sep) : _first{first}, _sep{sep} {}
//
//  friend raw_ostream& operator<<(raw_ostream& OS, const Separator& sep);
//
//  const HtmlEscape _first, _sep;
//  bool _printed = false;
//};
//
//inline HtmlEscape escape(const Twine& twine) {
//  return HtmlEscape{twine};
//}
//
//inline HtmlTag open(const Twine& tag, const Twine& attrs = "") {
//  return HtmlTag{tag, attrs, HtmlTag::Open};
//}
//inline HtmlTag close(const Twine& tag) {
//  return HtmlTag{tag, "", HtmlTag::Close};
//}
//inline HtmlTag simple(const Twine& tag) {
//  return HtmlTag{tag, "", HtmlTag::Empty};
//}
//
//inline SimpleHtml html(const Twine& tag, const Twine& msg) {
//  return SimpleHtml{tag, "", msg};
//}
//inline SimpleHtml html(const Twine& tag, const Twine &attrs, const Twine& msg) {
//  return SimpleHtml{tag, attrs, msg};
//}
//
//inline raw_ostream& operator<<(raw_ostream& OS, const HtmlEscape& esc) {
//  SmallString<32> buf;
//  StringRef str = esc._twine.toStringRef(buf);
//
//  for (char c : str) {
//    switch (c) {
//      case '&': OS << "&amp"; break;
//      case '<': OS << "&lt"; break;
//      case '>': OS << "&gt"; break;
//      case '"': OS << "&quot"; break;
//      default:  OS << c; break;
//    }
//  }
//
//  return OS;
//}
//
//inline raw_ostream& operator<<(raw_ostream& OS, const HtmlTag& tag) {
//  switch (tag._kind) {
//    case HtmlTag::Open:
//    case HtmlTag::Empty:
//      OS << '<';
//      break;
//    case HtmlTag::Close:
//      OS << "</";
//      break;
//    default:
//      llvm_unreachable("Invalid tag kind");
//  }
//
//  OS << escape(tag._tag);
//
//  {
//    SmallString<32> buf;
//    auto str = tag._attrs.toStringRef(buf);
//
//    if (!str.empty()) {
//      OS << ' ' << str;
//    }
//  }
//
//  switch (tag._kind) {
//    case HtmlTag::Open:
//    case HtmlTag::Close:
//      OS << '>';
//      break;
//    case HtmlTag::Empty:
//      OS << "/>";
//      break;
//    default:
//      llvm_unreachable("Invalid tag kind");
//  }
//
//  return OS;
//}
//
//inline raw_ostream& operator<<(raw_ostream& OS, const SimpleHtml& html) {
//  return OS << open(html._tag, html._attrs) << escape(html._content) << close(html._tag);
//}
//
//inline raw_ostream& operator<<(raw_ostream& OS, Separator& sep) {
//  if (sep._printed) {
//    OS << sep._sep;
//  } else {
//    OS << sep._first;
//    sep._printed = true;
//  }
//
//  return OS;
//}
//
//} // end namespace html


namespace html {

using namespace llvm;

struct Html {
  enum Kind {
    K_EscapedString,
    K_HtmlEntity,

    K_SimpleTag,   // tag with children, strings are escaped
    K_EmptyTag,    // tag without children
    K_VerbatimTag, // tag which contains only a string, string is *NOT* escaped

    K_String_First = K_EscapedString,
    K_String_Last  = K_HtmlEntity,

    K_Tag_First = K_SimpleTag,
    K_Tag_Last  = K_VerbatimTag,
  };

  /// Magic value for the indent parameter in print that means:
  /// Print in flow style, i.e., don't indent and don't add newlines.
  static constexpr const unsigned FLOW_STYLE = ~0u;

  virtual ~Html() {}

  void print(raw_ostream& OS, unsigned indent = 0) const {
    _print(OS, indent);
  }

  friend inline raw_ostream& operator<<(raw_ostream& OS, const Html& html) {
    html.print(OS);
    return OS;
  }

  Kind kind() const { return _kind; }
private:
  Html(Kind kind) : _kind{kind} {}

  virtual void _print(raw_ostream& OS, unsigned indent) const = 0;

  Kind _kind;

  friend class HtmlString;
  friend class HtmlTag;
};

struct HtmlString : Html {
  static bool classof(const Html *html) {
    return (html->kind() >= K_String_First) && (html->kind() <= K_String_Last);
  }

  virtual StringRef str() const = 0;
private:
  HtmlString(Html::Kind kind) : Html{kind} {}

  friend struct HtmlEscapedString;
  friend struct HtmlEntity;
};

/// HTML string where special characters are escaped before printing
struct HtmlEscapedString : HtmlString {
  HtmlEscapedString(std::string&& txt) : HtmlString{K_EscapedString}, _txt{std::move(txt)} {}
  HtmlEscapedString(const std::string& txt) : HtmlString{K_EscapedString}, _txt{txt} {}

  static bool classof(const Html *html) {
    return html->kind() == K_EscapedString;
  }

  StringRef str() const override { return _txt; }
private:
  void _print(raw_ostream& OS, unsigned indent) const override;

  std::string _txt;
};

struct HtmlEntity : HtmlString {
  enum Entity {
    TIMES,
    MINUS,
  };

  HtmlEntity(Entity entity) : HtmlString{K_HtmlEntity}, _entity{entity} {}

  static bool classof(const Html *html) {
    return html->kind() == K_HtmlEntity;
  }

  StringRef str() const override;
private:
  void _print(raw_ostream& OS, unsigned indent) const override;

  Entity _entity;
};

struct HtmlAttr {
  /// Create a value-less attribute (like `checked' in checkbox inputs)
  explicit HtmlAttr(const Twine& name) : _name{name.str()} {}

  HtmlAttr(const std::string& name, const std::string& value) : _name{name}, _value{value} {}

  StringRef name() const { return _name; }

  Optional<std::string>& value() { return _value; }
  const Optional<std::string>& value() const { return _value; }

  void print(raw_ostream& OS, unsigned indent = 0) const;
private:
  std::string _name;
  Optional<std::string> _value;
};

struct HtmlTag : Html {
  using Attrs = std::vector<HtmlAttr>;

  StringRef tag() const { return _tag.str(); }

  Optional<StringRef> getAttr(StringRef name) {
    for (auto& attr : _attrs) {
      if (attr.name() == name) {
        if (auto &val = attr.value()) {
          return StringRef{*val};
        } else {
          return None;
        }
      }
    }
    return None;
  }

  void addAttr(HtmlAttr attr) {
    _attrs.push_back(attr);
  }
  void addAttr(const Twine& name) {
    _attrs.push_back(HtmlAttr{name});
  }
  void addAttr(const std::string& name, const std::string& value) {
    _attrs.push_back({name, value});
  }

  void addAttrs() {}

    template<typename... Args>
  void addAttrs(const HtmlAttr& attr, Args&&... args) {
    _attrs.push_back(attr);
    addAttrs(std::forward<Args>(args)...);
  }

  void addClass(const std::string& css_class) {
    for (auto& attr : _attrs) {
      if (attr.name() == "class") {
        assert(attr.value() && "`class' attribute without value!");

        *attr.value() += ' ';
        *attr.value() += css_class;
        return;
      }
    }

    addAttr("class", css_class);
  }

  static bool classof(const Html *html) {
    return (html->kind() >= K_Tag_First) && (html->kind() <= K_Tag_Last);
  }

  const Attrs& attrs() const { return _attrs; }
private:
  HtmlTag(Html::Kind kind, const Twine& tag, const std::initializer_list<HtmlAttr>& attrs)
    : Html{kind}
    ,_attrs{attrs}
  {
    tag.toVector(_tag);
  }

  friend struct SimpleTag;
  friend struct EmptyTag;
  friend struct VerbatimTag;

  SmallString<16> _tag;
  Attrs           _attrs;
};

struct SimpleTag : HtmlTag {
  enum Style {
    FlowStyle,  // print children without inserting newlines
    BlockStyle, // print each child on its own line, properly indented.
  };

  using Body = std::vector<Html*>;

  SimpleTag(const Twine& tag, const std::initializer_list<HtmlAttr>& attrs, const std::initializer_list<Html*>& body, Style style = BlockStyle)
  : HtmlTag{Html::K_SimpleTag, tag, attrs}, _style{style}, _body{body} {}

  void addChild(Html* html) {
    _body.push_back(html);
  }

  void add() {}

  template<typename Arg, typename... Args>
  void add(Arg&& arg, Args&&... args) {
    _add(arg);
    add(std::forward<Args>(args)...);
  }

  template<typename Visitor>
  void accept(Visitor&& visitor);

  static bool classof(const Html *html) {
    return html->kind() == K_SimpleTag;
  }

  Style style() const { return _style; }
  void setStyle(Style style) { _style = style; }

  SimpleTag* withStyle(Style style) {
    setStyle(style);
    return this;
  }

  Body::iterator begin() { return _body.begin(); }
  Body::iterator end()   { return _body.end(); }
private:
  void _add(Html* html) {
    _body.push_back(html);
  }

  void _add(const HtmlAttr& attr) {
    _attrs.push_back(attr);
  }

  void _add(StringRef txt) {
    _body.push_back(new HtmlEscapedString{txt.str()});
  }

  void _print(raw_ostream& OS, unsigned indent) const override;

  Style _style;
  Body  _body;
};

/// Html tag without children, like <br>, <input>
struct EmptyTag : HtmlTag {
  EmptyTag(const Twine& tag) : HtmlTag{K_EmptyTag, tag, {}} {}

  static bool classof(const Html *html) {
    return html->kind() == K_EmptyTag;
  }
private:
  void _print(raw_ostream& OS, unsigned indent) const override;
};

struct VerbatimTag : HtmlTag {
  VerbatimTag(const Twine& tag) : HtmlTag{K_VerbatimTag, tag, {}} {}

  VerbatimTag(const Twine& tag, const Twine& source) : HtmlTag{K_VerbatimTag, tag, {}}, _body{source.str()} {}

  static bool classof(const Html *html) {
    return html->kind() == K_VerbatimTag;
  }

  StringRef body() const { return _body.str(); }

  void append(const Twine& t) {
    t.toVector(_body);
  }
private:
  void _print(raw_ostream& OS, unsigned indent) const override;

  SmallString<16> _body;
};

template<typename Visitor>
void SimpleTag::accept(Visitor&& visitor) {
  visitor(this);

  for (auto child : _body) {
    switch (child->kind()) {
      case K_SimpleTag:
        cast<SimpleTag>(child)->accept(std::forward<Visitor>(visitor));
        break;
      case K_EmptyTag:
        visitor(cast<EmptyTag>(child));
        break;
      case K_VerbatimTag:
        visitor(cast<VerbatimTag>(child));
        break;
      case K_EscapedString:
      case K_HtmlEntity:
        break;
      default:
        llvm_unreachable("Garbage kind in HTML AST node!");
    }
  }
}

using HtmlAttrs = std::initializer_list<HtmlAttr>;
using HtmlBody  = std::initializer_list<Html*>;


// ***** TAG CTOR HELPERS

#define DEFINE_TAG_CTORS(NAME) \
  inline SimpleTag* NAME() { \
    return new SimpleTag{#NAME, {}, {}}; \
  } \
  template<typename... Args> \
  inline SimpleTag* NAME(Args... args) { \
    auto tag = new SimpleTag{#NAME, {}, {}}; \
    tag->add(args...); \
    return tag; \
  }

inline HtmlEscapedString* str(const Twine& txt) {
  return new HtmlEscapedString{txt.str()};
}

template<typename... Args> \
  inline SimpleTag* tag(const Twine& tag, Args... args) {
    auto html = new SimpleTag{tag, {}, {}};
    html->add(args...);
    return html;
}

DEFINE_TAG_CTORS(div)
DEFINE_TAG_CTORS(a)
DEFINE_TAG_CTORS(span)
DEFINE_TAG_CTORS(table)
DEFINE_TAG_CTORS(tbody)
DEFINE_TAG_CTORS(thead)
DEFINE_TAG_CTORS(tr)
DEFINE_TAG_CTORS(th)
DEFINE_TAG_CTORS(td)

inline EmptyTag* br() {
  return new EmptyTag{"br"};
}

template<typename... Attrs>
inline EmptyTag* input(const Twine& type, Attrs&&... attrs) {
  auto tag = new EmptyTag{"input"};
  tag->addAttr("type", type.str());
  tag->addAttrs(std::forward<Attrs>(attrs)...);
  return tag;
}

template<typename... Attrs>
inline EmptyTag* meta(Attrs&&... attrs) {
  auto tag = new EmptyTag{"meta"};
  tag->addAttrs(std::forward<Attrs>(attrs)...);
  return tag;
}

template<typename... Attrs>
inline EmptyTag* img(Attrs&&... attrs) {
  auto tag = new EmptyTag{"img"};
  tag->addAttrs(std::forward<Attrs>(attrs)...);
  return tag;
}

inline VerbatimTag* script(const Twine& source) {
  return new VerbatimTag{"script", source};
}

inline VerbatimTag* style(const Twine& source) {
  return new VerbatimTag{"style", source};
}

inline HtmlAttr attr(const Twine& name, const Twine& value) {
  return HtmlAttr{name.str(), value.str()};
}

inline HtmlAttr attr(const Twine& name, unsigned value) {
  return HtmlAttr{name.str(), std::to_string(value)};
}

inline HtmlAttr data_attr(const Twine& name, const Twine& value) {
  return HtmlAttr{("data-" + name).str(), value.str()};
}

inline HtmlAttr css_id(const Twine& t) {
  return HtmlAttr{"id", t.str()};
}

inline HtmlAttr css_class(const Twine& t) {
  return HtmlAttr{"class", t.str()};
}


// ***** HTML ENTITIES

inline HtmlEntity* times() {
  return new HtmlEntity(HtmlEntity::TIMES);
}

inline HtmlEntity* minus() {
  return new HtmlEntity(HtmlEntity::MINUS);
}


// ***** PRINT HELPERS

void print_str(raw_ostream& OS, StringRef str);

void print_empty(raw_ostream& OS, const Twine& tag);
void print_empty(raw_ostream& OS, const Twine& tag, const HtmlAttrs& attrs);

void print_open(raw_ostream& OS, const Twine& tag);
void print_open(raw_ostream& OS, const Twine& tag, const HtmlAttrs& attrs);

void print_close(raw_ostream& OS, const Twine& tag);

} // end namespace html
