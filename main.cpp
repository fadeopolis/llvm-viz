// This file is distributed under the Revised BSD Open Source License.
// See LICENSE.TXT for details.

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>                 // for Twine
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>         // for LoopInfoWrapperPass
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/LLVMContext.h>            // for getGlobalContext
#include <llvm/IR/LegacyPassManager.h>      // for PassManager
#include <llvm/IR/Module.h>                 // for Module
#include <llvm/IR/ModuleSlotTracker.h>      // for ModuleSlotTracker
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>          // for initializeAnalysis
#include <llvm/Pass.h>                      // for ModulePass
#include <llvm/PassAnalysisSupport.h>       // for AnalysisUsage
#include <llvm/PassRegistry.h>              // for PassRegistry
#include <llvm/PassSupport.h>               // for INITIALIZE_PASS
#include <llvm/Support/CommandLine.h>       // for desc, ParseCommandLineOptions, opt, value_desc, FormattingFlags::Positional
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/PrettyStackTrace.h>  // for PrettyStackTraceProgram
#include <llvm/Support/raw_ostream.h>       // for raw_ostream, outs
#include <llvm/Support/Signals.h>           // for PrintStackTraceOnErrorSignal
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <memory>                           // for unique_ptr
#include <string>                           // for string
#include "HtmlUtils.hpp"
#include "PassUtils.hpp"
#include "PrintUtils.hpp"
#include "Utils.hpp"

#include "jQuerySource.hpp"
#include "BootstrapJsSource.hpp"
#include "BootstrapCssSource.hpp"

using namespace llvm;
using namespace html;

static cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<IR file>"));
static cl::opt<std::string> OutputFilename("o", cl::desc("Output filename"), cl::value_desc("filename"));

/// I've had enough *&^#$ memory corruption bugs with LLVMs legacy passmanager/analysis-cache.
/// We'll just compute the stuff we need ourselves and basta.
struct Analyses {
  Analyses(Module& m)
  : _module{m}
  , _slots{&m, true}
  , _inst_namer{_slots}
  , _tlii{Triple{m.getTargetTriple()}}
  , _tli{_tlii}
  {}

  void recalculate(Function& fn) {
    _function.reset(&fn);

    _domTree.recalculate(fn);
    _loops.analyze(_domTree);

    _assumptions.reset(new AssumptionCache{fn});

    _scev.reset(new ScalarEvolution{
      *_function,
      _tli,
      *_assumptions,
      _domTree,
      _loops
    });
  }

  Module& module() {
    return _module;
  }

  Function& function() {
    return *_function;
  }

  InstructionNamer& names() {
    return _inst_namer;
  }

  LoopInfo& loops() {
    return _loops;
  }
  ScalarEvolution& scev() {
    return *_scev;
  }

  /// ***** module global
  Module& _module;
  ModuleSlotTracker _slots;
  InstructionNamer _inst_namer;
  TargetLibraryInfoImpl _tlii;
  TargetLibraryInfo _tli;

  /// ***** per fn
  safe_ptr<Function> _function; // the function these analyses are valid for
  DominatorTree _domTree;
  LoopInfo _loops;
  std::unique_ptr<AssumptionCache> _assumptions;
  std::unique_ptr<ScalarEvolution> _scev;
};

/// Responsible for rendering one or more columns of the instruction table, each representing one instruction attribute,
/// like name, opcode, operands, etc.
struct Renderer {
  virtual ~Renderer() {}

  /// Renders HTML for one instruction attribute.
  /// One renderer may spawn multiple AttributeRenderers
  struct AttributeRenderer {
    virtual ~AttributeRenderer() {}

    /// Renders the column header containing the name/description of the attribute this renderer visualizes.
    virtual Html* renderColumnHeader() = 0;

    /// Render HTML for instruction attribute.
    /// The returned HTML will be wrapped into a <td> tag in the instruction table.
    virtual Html* render(const Instruction& inst) = 0;

    /// Helper for the common case of just rendering a simple string.
    Html* renderStr(const std::string& str) {
      return html::str(str);
    }
  };

  /// Adjusts the style of the tbody for a BasicBlock
  struct BasicBlockStyler {
    /// Called once all instructions has been rendered to allow the styler to adjust CSS classes, etc.
    /// This is not supposed to add new elements, though the API currently does not prevent it.
    virtual void style(const BasicBlock& bb, SimpleTag* tbody) = 0;
  };

  struct CssFlag {
    CssFlag(const Twine& display, const Twine& css, bool init = true)
    : displayName{display.str()}
    , cssName{css.str()}
    , initial_value{init}
    {}

    std::string displayName, cssName;
    bool initial_value;
  };

  virtual void addCssFlags(VectorAppender<CssFlag> dst) = 0;

  /// Called once everything has been rendered to allow for additional tweaks.
  // i.e. adding some CSS classes or adding <script> tags, etc.
  virtual void styleBody(SimpleTag* body) = 0;

  /// Create instruction attribute renderers
  /// This function is called once per each llvm::Function
  virtual void createRenderers(Analyses&, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) = 0;

  /// This function is called once per each llvm::Function
  virtual void createBasicBlockStylers(Analyses&, VectorAppender<std::unique_ptr<BasicBlockStyler>> dst) = 0;
private:
  /// Helper class that just calls a lambda to render an attribute
  struct LambdaRenderer final : AttributeRenderer {
    LambdaRenderer(const std::function<Html*()>& header, const std::function<Html*(const Instruction&)>& attr)
    : _renderHeader{header}, _renderAttr{attr} {}

    Html* renderColumnHeader() override { return _renderHeader(); }
    Html* render(const Instruction& inst) override { return _renderAttr(inst); }
  private:
    std::function<Html*()> _renderHeader;
    std::function<Html*(const Instruction&)> _renderAttr;
  };

  /// Helper class that just calls a lambda to style a block
  struct LambdaBasicBlockStyler final : BasicBlockStyler {
    LambdaBasicBlockStyler(const std::function<void(const BasicBlock&, SimpleTag*)>& styler)
    : _styler{styler} {}

    void style(const BasicBlock& bb, SimpleTag* tag) override {
      _styler(bb, tag);
    }
  private:
    std::function<void(const BasicBlock&, SimpleTag*)> _styler;
  };
protected:
  /// Helper for creating a simple renderer from lambdas
  void createRenderer(
      VectorAppender<std::unique_ptr<AttributeRenderer>> dst,
      std::function<Html*()> renderHeader,
      std::function<Html*(const Instruction&)> renderAttr
  ) {
    dst.emplace_back(new LambdaRenderer{renderHeader, renderAttr});
  }

  /// Helper for creating a simple styler from lambdas
  void createStyler(
    VectorAppender<std::unique_ptr<BasicBlockStyler>> dst,
    std::function<void(const BasicBlock&, SimpleTag*)> styler
  ) {
    dst.emplace_back(new LambdaBasicBlockStyler{styler});
  }
};

/// Helper class for implementing renderers. This implementation just renders nothing
struct DummyRenderer : Renderer {
  void styleBody(SimpleTag* body) override {}

  void addCssFlags(VectorAppender<CssFlag> dst) override {}

  /// Create instruction attribute renderers
  void createRenderers(Analyses&, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {}

  void createBasicBlockStylers(Analyses&, VectorAppender<std::unique_ptr<BasicBlockStyler>> dst) override {}
};


//**********************************************************************************************************************
// RENDERER IMPLEMENTATIONS

/// Render instruction name (if present)
struct NameRenderer : DummyRenderer {
  void createRenderers(Analyses& analyses, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {
    createRenderer(
      dst,
      [&]() {
        return html::str("Name");
      },
      [&](const Instruction& inst) {
        if (!inst.getType()->isVoidTy()) {
          return html::str(analyses.names().asOperand(inst));
        } else {
          return html::str("");
        }
      }
    );
  }
};

/// Render instruction type
struct TypeRenderer : DummyRenderer {
  void createRenderers(Analyses& analyses, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {
    createRenderer(
      dst,
      [&]() {
        return html::str("Type");
      },
      [&](const Instruction& inst) {
        auto ty = inst.getType();

        std::string buf;
        raw_string_ostream OS{buf};

        ty->print(OS, false);
        OS.flush();

        return html::str(buf);
      }
    );
  }
};

/// Render instruction opcode
struct OpcodeRenderer : DummyRenderer {
  void createRenderers(Analyses& analyses, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {
    createRenderer(
        dst,
        [&]() {
          return html::str("Opcode");
        },
        [&](const Instruction& inst) {
          return html::str(inst.getOpcodeName());
        }
    );
  }
};

struct OperandsRenderer : DummyRenderer {
  void createRenderers(Analyses& analyses, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {
    createRenderer(
      dst,
      [&]() {
        return html::str("Operands");
      },
      [&](const Instruction& inst) {
        auto wrapper = div();

        for (auto op : const_cast<Instruction&>(inst).operand_values()) {
          wrapper->add(analyses.names().ref(op));
          wrapper->add(br());
        }

        return wrapper;
      }
    );
  }
};

struct ScevRenderer : DummyRenderer {
  struct Visitor : SCEVVisitor<Visitor, void> {
    Visitor(InstructionNamer& namer) : _namer{namer}, _html{div()} {}

    void visitConstant(const SCEVConstant* expr) {
      emit(_namer.ref(expr->getValue()));
    }
    void visitTruncateExpr(const SCEVTruncateExpr* expr) {
      emit("(trunc " + html::print(*expr->getOperand()->getType()) + " ");
      visit(expr->getOperand());
      emit(" to " + html::print(*expr->getOperand()->getType()) + ")");
    }
    void visitSignExtendExpr(const SCEVSignExtendExpr* expr) {
      emit("(sext " + html::print(*expr->getOperand()->getType()) + " ");
      visit(expr->getOperand());
      emit(" to " + html::print(*expr->getOperand()->getType()) + ")");
    }
    void visitZeroExtendExpr(const SCEVZeroExtendExpr* expr) {
      emit("(zext " + html::print(*expr->getOperand()->getType()) + " ");
      visit(expr->getOperand());
      emit(" to " + html::print(*expr->getOperand()->getType()) + ")");
    }
    void visitAddExpr(const SCEVAddExpr* expr) {
      visitNAry(expr, " + ");
    }
    void visitMulExpr(const SCEVMulExpr* expr) {
      visitNAry(expr, " * ");
    }
    void visitUDivExpr(const SCEVUDivExpr* expr) {
      emit("(");
      visit(expr->getLHS());
      emit("/u");
      visit(expr->getRHS());
      emit(")");
    }
    void visitAddRecExpr(const SCEVAddRecExpr* AR) {
      emit("{");
      visit(AR->getOperand(0));

      for (unsigned i = 1, e = AR->getNumOperands(); i != e; ++i) {
        emit(" ,+, ");
        visit(AR->getOperand(i));
      }

      emit("}<");

      if (AR->hasNoUnsignedWrap())
        emit("nuw><");
      if (AR->hasNoSignedWrap())
        emit("nsw><");
      if (AR->hasNoSelfWrap() && !AR->getNoWrapFlags((SCEV::NoWrapFlags) (SCEV::FlagNUW | SCEV::FlagNSW)))
        emit("nw><");

      emit(_namer.ref(AR->getLoop()->getHeader()));
      emit(">");
    }
    void visitSMaxExpr(const SCEVSMaxExpr* expr) {
      visitNAry(expr, " smax ");
    }
    void visitUMaxExpr(const SCEVUMaxExpr* expr) {
      visitNAry(expr, " umax ");
    }
    void visitUnknown(const SCEVUnknown* U) {
      Type *AllocTy;
      if (U->isSizeOf(AllocTy)) {
        emit("sizeof(" + html::print(*AllocTy) + ")");
        return;
      }
      if (U->isAlignOf(AllocTy)) {
        emit("alignof(" + html::print(*AllocTy) + ")");
        return;
      }

      Type *CTy;
      Constant *FieldNo;
      if (U->isOffsetOf(CTy, FieldNo)) {
        emit("offsetof(" + html::print(*CTy) + ", ", _namer.ref(FieldNo), ")");
        return;
      }

      // Otherwise just print it normally.
      emit(_namer.ref(U->getValue()));
    }

    void visitNAry(const SCEVNAryExpr* NAry, StringRef op) {
      emit("(");

      for (SCEVNAryExpr::op_iterator I = NAry->op_begin(), E = NAry->op_end(); I != E; ++I) {
        visit(*I);

        if (std::next(I) != E)
          emit(op);
      }
      emit(")");

      switch (NAry->getSCEVType()) {
        case scAddExpr:
        case scMulExpr:
          if (NAry->hasNoUnsignedWrap())
            emit("<nuw>");
          if (NAry->hasNoSignedWrap())
            emit("<nuw>");
      }
    }

    SimpleTag* html() {
      return _html;
    }
  private:
    template<typename... T>
    void emit(T&&... t) {
      _html->add(std::forward<T>(t)...);
    }

    InstructionNamer& _namer;
    SimpleTag*        _html;
  };

  void createRenderers(Analyses& analyses, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {
    createRenderer(
      dst,
      [&]() {
        return html::str("SCEV");
      },
      [&](const Instruction& inst) mutable -> Html* {
        auto& inst_namer = analyses.names();
        auto& loops      = analyses.loops();
        auto& scev       = analyses.scev();

        if (!scev.isSCEVable(inst.getType()) || !loops.getLoopFor(inst.getParent()))
          return div();

        auto expr = scev.getSCEV(const_cast<Instruction*>(&inst));
        assert(expr);

        Visitor v{inst_namer};
        v.visit(expr);

        return v.html()->withStyle(SimpleTag::FlowStyle);
      }
    );
  }
};

/// adds loop depth info to basic blocks as html attribute `data-loop-depth'
struct LoopDepthStyler final : DummyRenderer {
  static constexpr const unsigned MAX_DEPTH() { return  7; }

  void createBasicBlockStylers(Analyses& analyses, VectorAppender<std::unique_ptr<BasicBlockStyler>> dst) override {
    createStyler(dst, [&](const BasicBlock& bb, SimpleTag* tbody) {
      auto& loop_info = analyses.loops();

      unsigned loop_depth = std::min(loop_info.getLoopDepth(&bb), MAX_DEPTH());

      std::string even = "loop-" + std::to_string(loop_depth) + "-even";
      std::string odd  = "loop-" + std::to_string(loop_depth) + "-odd";

      unsigned i = 0;
      for (auto* row : *tbody) {
        cast<HtmlTag>(row)->addClass((i % 2 == 0) ? even : odd);
        i++;
      }
    });
  }

  // TODO: remove, just for demo purposes
//  void createRenderers(Analyses& analyses, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {
//    createRenderer(
//      dst,
//      [&]() {
//        return new HtmlString("loop-depth");
//      },
//      [&](const Instruction& inst) {
//        auto& loop_info = analyses.loops();
//
//        return new HtmlString{std::to_string(loop_info.getLoopDepth(inst.getParent()))};
//      }
//    );
//  }

  void addCssFlags(VectorAppender<CssFlag> dst) {
    dst.emplace_back("Loop depth color", "loop-depth-color", true);
  }
};


//**********************************************************************************************************************
// Main Pass for actually printing HTML for a Module.

struct HtmlPrinter {
  HtmlPrinter(Module& m) : analyses{m} {}

  bool run(raw_ostream& OS) {
    /// Register renderers for instruction attributes we visualize

    _renderers.emplace_back(new NameRenderer{});
    _renderers.emplace_back(new TypeRenderer{});
    _renderers.emplace_back(new OpcodeRenderer{});
    _renderers.emplace_back(new OperandsRenderer{});
    _renderers.emplace_back(new LoopDepthStyler{});
    _renderers.emplace_back(new ScevRenderer{});

    /// Hard coded CSS

    OS << "<!DOCTYPE html>\n";

    auto doc = tag("html", attr("lang", "en"));

    {
      auto head = tag("head");

      head->add(
        meta(attr("charset", "utf-8")),
        meta(attr("http-equiv", "X-UA-Compatible"), attr("content", "IE=edge")),
        meta(attr("name", "viewport"), attr("content", "width=device-width, initial-scale=1")),
        meta(attr("name", "description"), attr("content", "llvm-IR visualization")),
        meta(attr("name", "author"), attr("content", "Fader A. Vader"))
      );

      head->add(style(R"(
        /******************************************/
        /* general table styles */

        th { text-align: center; }

        /******************************************/
        /* color even/odd table rows differently */
        /* we also encode loop nesting in colors */
        /* disable colors with class `loop-depth-color' on <body> */

        tr.loop-0-odd  { background-color: rgb(255,255,255); }
        tr.loop-0-even { background-color: rgb(220,220,220); }

        tr.loop-1-odd  { background-color: rgb(255,255,255); }
        tr.loop-1-even { background-color: rgb(220,220,220); }

        tr.loop-2-odd  { background-color: rgb(255,255,255); }
        tr.loop-2-even { background-color: rgb(220,220,220); }

        tr.loop-3-odd  { background-color: rgb(255,255,255); }
        tr.loop-3-even { background-color: rgb(220,220,220); }

        tr.loop-4-odd  { background-color: rgb(255,255,255); }
        tr.loop-4-even { background-color: rgb(220,220,220); }

        tr.loop-5-odd  { background-color: rgb(255,255,255); }
        tr.loop-5-even { background-color: rgb(220,220,220); }

        tr.loop-6-odd  { background-color: rgb(255,255,255); }
        tr.loop-6-even { background-color: rgb(220,220,220); }

        tr.loop-7-odd  { background-color: rgb(255,255,255); }
        tr.loop-7-even { background-color: rgb(220,220,220); }

        body.loop-depth-color tr.loop-0-odd  { background-color: rgb(255,255,255); }
        body.loop-depth-color tr.loop-0-even { background-color: rgb(220,220,220); }

        body.loop-depth-color tr.loop-1-odd  { background-color: rgb(254,240,217); }
        body.loop-depth-color tr.loop-1-even { background-color: rgb(253,212,158); }

        body.loop-depth-color tr.loop-2-odd  { background-color: rgb(254,240,217); }
        body.loop-depth-color tr.loop-2-even { background-color: rgb(253,212,158); }

        body.loop-depth-color tr.loop-3-odd  { background-color: rgb(253,212,158); }
        body.loop-depth-color tr.loop-3-even { background-color: rgb(253,187,132); }

        body.loop-depth-color tr.loop-4-odd  { background-color: rgb(252,141,089); }
        body.loop-depth-color tr.loop-4-even { background-color: rgb(239,101,072); }

        body.loop-depth-color tr.loop-5-odd  { background-color: rgb(215,048,031); }
        body.loop-depth-color tr.loop-5-even { background-color: rgb(215,048,031); }

        body.loop-depth-color tr.loop-6-odd  { background-color: rgb(215,048,031); }
        body.loop-depth-color tr.loop-6-even { background-color: rgb(215,048,031); }

        body.loop-depth-color tr.loop-7-odd  { background-color: rgb(215,048,031); }
        body.loop-depth-color tr.loop-7-even { background-color: rgb(215,048,031); }


        /******************************************/
        /* collapsable code for function */

        /*
         * the collapse/expand `button' is implemented as a link,
         * disable underlining and selection to make it feel more like a button
         */
        a.nounderline {
           text-decoration: none !important;
           user-select: none;
        }

        .function-code {
          transition: 0.5s; /* 0.5 second transition effect to slide in or slide down the overlay */
        }


        /******************************************/
        /* collapsable overlay for showing CFG */



        /******************************************/
        /* fixed position bar on top of page with checkboxes for enabling/disabling display flags */

        #control-bar {
            position: fixed;
            top: 0;
            right: 0;
            z-index: 999;
            background-color: rgb(220,220,220);
            border-radius: 0 0 0 1em;
            padding: 1em;
            width: auto;
        }
      )"));
      head->add(style(BootstrapCssSource()));

      head->add(tag("title", analyses.module().getModuleIdentifier()));

      doc->add(head);
    }

    auto body = tag("body");

    {
      auto control_bar = table(attr("id", "control-bar"), attr("class", "table"));

      std::vector<Renderer::CssFlag> css_flags;

      for (auto& renderer : _renderers)
        renderer->addCssFlags(css_flags);

      for (auto& flag : css_flags) {
        control_bar->add(
          tr(
            th(
              html::input(
                "checkbox",
                attr("checked", flag.initial_value ? "checked" : "unchecked"),
                attr("onclick", "setBodyFlag(this.checked,'" + flag.cssName + "');")
              )
            ),
            th(flag.displayName)
          )
        );
      }

      body->add(control_bar);
    }

    for (auto& fn : analyses.module()) {
      if (fn.empty())
        continue;

      auto fn_html = emitFunction(fn);

      body->add(fn_html);
    }

    body->add(script(jQuerySource()));
    body->add(script(BootstrapJsSource()));

    /// JS for enabling/disabling the loop-depth-color-flag
    body->add(script(R"(
        function setBodyFlag(enable, css_class) {
          if (enable) {
            $('body').addClass(css_class);
          } else {
            $('body').removeClass(css_class);
          }
        }

        setBodyFlag(true, 'loop-depth-color');
    )"));

    /// JS for collapsing/expanding code for a function
    body->add(script(R"(
        function collapseCode(button, fn_code) {
          if (fn_code.is(':visible')) {
            button.html('&minus;');
          } else {
            button.html('&times;');
          }

          fn_code.toggle();
        }
    )"));

    body->add(script(R"XO(
</script>
<svg height="130" width="500">
  <defs>
    <linearGradient id="grad1" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%"
      style="stop-color:rgb(255,255,0);stop-opacity:1" />
      <stop offset="100%"
      style="stop-color:rgb(255,0,0);stop-opacity:1" />
    </linearGradient>
  </defs>
  <ellipse cx="100" cy="70" rx="85" ry="55" fill="url(#grad1)" />
                     <text fill="#ffffff" font-size="45" font-family="Verdana"
    x="50" y="86">SVG</text>
             Sorry, your browser does not support inline SVG.
    </svg>
<script>
    )XO"));

    doc->add(body);

    doc->print(OS, 0);

    return false;
  }
private:
  Html* emitFunction(Function& fn) {
    analyses.recalculate(fn);

    _attrs.clear();
    for (auto& renderer : _renderers)
      renderer->createRenderers(analyses, _attrs);

    _basic_block_stylers.clear();
    for (auto& renderer : _renderers)
      renderer->createBasicBlockStylers(analyses, _basic_block_stylers);

    auto main = div(attr("id", getId(fn)));

    /// render header with function name & fold/unfold button
    {
      auto header = tag("h1");

      header->add(
        tag("a",
          attr("class", "nounderline"),
          attr("onclick", "collapseCode($(this), $('#" + fn.getName().str() + "-code'));"),
          times()
        )
      );

      header->add(fn.getName());

      main->add(header);
    }

    auto fn_html = div(attr("class", "function-code"), attr("id", fn.getName().str() + "-code"));

    /// render table for function arguments`
    {
      auto table = html::table(attr("class", "table arg-table"));

      bool first = true;
      for (auto& arg : fn.getArgumentList()) {
        auto row = html::tr();

        auto label = th();

        if (first)
          label->addChild(html("Args:"));

        first = false;

        row->add(
          label,
          th(attr("id", getId(arg)), html(arg)),
          th(html(arg.getType()))
        );

        table->add(row);
      }

      fn_html->add(table);
    }

    /// render table for function code
    {
      auto block_table = table();
      block_table->addAttr("class", "table block-table");

      for (auto &block : fn) {
        auto *tbody = emitBasicBlock(block);

        for (auto &styler : _basic_block_stylers)
          styler->style(block, tbody);

        block_table->add(tbody);
        block_table->add(tr());
      }

      fn_html->add(block_table);
    }

    main->add(fn_html);
    return main;
  }

  SimpleTag* emitBasicBlock(BasicBlock& bb) {
    unsigned num_columns = std::max<size_t>(3u, _attrs.size());

    auto body = html::tbody();
    body->addAttr("id", getId(bb));

    body->addClass("basic-block");

    /// emit general info for basic block
    {
      auto lbl_colspan = attr("colspan", div_round_down(num_columns, 2));
      auto txt_colspan = attr("colspan", div_round_up(num_columns, 2));

      body->add(
        tr(
          th(lbl_colspan, html("Basic block:")),
          td(txt_colspan, html(bb))
        )
      );

      body->add(
        tr(
          th(lbl_colspan, html("Predecessors:")),
          td(txt_colspan, [&](){
            auto wrapper = div();

            Separator sep;

            for (auto pred : predecessors(&bb)) {
              wrapper->add(
                sep.str(),
                ref(pred)
              );
            }

            return wrapper->withStyle(SimpleTag::FlowStyle);
          }())
        )
      );

      body->add(
        tr(
          th(lbl_colspan, html("Successors:")),
          td(txt_colspan, [&](){
            auto wrapper = div();

            Separator sep;

            for (auto succ : successors(&bb)) {
              wrapper->add(
                sep.str(),
                ref(succ)
              );
            }

            return wrapper->withStyle(SimpleTag::FlowStyle);
          }())
        )
      );
    }

    /// emit table legend
    {
      auto row = tr();

      for (auto& attr : _attrs)
        row->add(th(attr->renderColumnHeader()));

      body->add(row);
    }

    for (auto& inst : bb)
      body->addChild(emitInstruction(inst));

    return body;
  }

  SimpleTag* emitInstruction(Instruction& inst) {
    auto row = html::tr();

    row->addClass("instruction");

    if (!inst.getType()->isVoidTy())
      row->addAttr("id", getId(inst));

    assert(!_attrs.empty());

    /// let renderers emit the individual columns for each attribute
    for (auto& attr : _attrs) {
      auto elem = attr->render(inst);

      row->addChild(td(elem));
    }

    return row;
  }

  HtmlString* html(const Twine& txt) {
    return html::str(txt.str());
  }
  HtmlString* html(const Value* val) {
    assert(val);
    return html(*val);
  }
  HtmlString* html(const Value& val) {
    return html::str(analyses.names().asOperand(val));
  }
  HtmlString* html(const Type* ty) {
    assert(ty);
    return html(*ty);
  }
  HtmlString* html(const Type& ty) {
    std::string buf;
    raw_string_ostream OS{buf};

    ty.print(OS, false);
    OS.flush();

    return html::str(buf);
  }
  Html* html(Html* html) {
    return html;
  }

  std::string getId(const Value& v) {
    return analyses.names().getId(v);
  }

  Html* ref(const Value* v) {
    return analyses.names().ref(v);
  }

  static size_t div_round_up(size_t a, size_t b) {
    return (a + b - 1) / b;
  }
  static size_t div_round_down(size_t a, size_t b) {
    return a / b;
  }

  Analyses analyses;

  std::vector<std::unique_ptr<Renderer>> _renderers;
  std::vector<std::unique_ptr<Renderer::AttributeRenderer>> _attrs;
  std::vector<std::unique_ptr<Renderer::BasicBlockStyler>> _basic_block_stylers;
};

int main(int argc, const char * const* argv) {
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X{argc, argv};

  LLVMContext &Context = getGlobalContext();

  cl::ParseCommandLineOptions(argc, argv, "LLVM-IR HTML visualizer\n");

  // Load IR of the module to be compiled...
  std::unique_ptr<Module> M = [&](){
    SMDiagnostic Err;
    auto M = parseIRFile(InputFilename, Err, Context);
    if (!M) {
      Err.print(argv[0], errs());
      exit(1);
    }
    return M;
  }();

  HtmlPrinter printer{*M};

  // Open the output file.

  if (OutputFilename.empty() || (OutputFilename == "-")) {
    printer.run(outs());
  } else {
    std::error_code EC;
    tool_output_file TOF{OutputFilename, EC, sys::fs::F_Text};

    if (EC) {
      errs() << argv[0] << ": Could not open output file `" << OutputFilename << "': " << EC.message() << '\n';
      exit(1);
    }

    printer.run(TOF.os());
    TOF.keep();
  }

  return 0;
}
