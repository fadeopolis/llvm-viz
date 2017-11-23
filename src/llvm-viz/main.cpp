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
#include "ValueNameMangler.hpp"
#include "CfgToDot.hpp"
#include "Style.hpp"
#include <support/VectorAppender.hpp>
#include <support/safe_ptr.hpp>
#include <support/VectorAppender.hpp>
#include <support/PrintUtils.hpp>

/// generated sources
#include "generated/jQuerySource.hpp"
#include "generated/BootstrapJsSource.hpp"
#include "generated/BootstrapCssSource.hpp"

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

  ValueNameMangler& names() {
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
  ValueNameMangler _inst_namer;
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

  // *********************************************************************************
  // ***** RENDER FUNCTION CODE

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
    /// Called once all instructions have been rendered to allow the styler to adjust CSS classes, etc.
    /// This is not supposed to add new elements, though the API currently does not prevent it.
    virtual void style(const BasicBlock& bb, SimpleTag* tbody) = 0;
  };

  /// Create instruction attribute renderers
  /// This function is called once per each llvm::Function
  virtual void createRenderers(Analyses&, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) = 0;

  /// This function is called once per each llvm::Function
  virtual void createBasicBlockStylers(Analyses&, VectorAppender<std::unique_ptr<BasicBlockStyler>> dst) = 0;

protected:
  /// Helper for creating a simple renderer from lambdas
  void createRenderer(
    VectorAppender<std::unique_ptr<AttributeRenderer>> dst,
    std::function<Html*()> renderHeader,
    std::function<Html*(const Instruction&)> renderAttr
  );

  /// Helper for creating a simple styler from lambdas
  void createStyler(
    VectorAppender<std::unique_ptr<BasicBlockStyler>> dst,
    std::function<void(const BasicBlock&, SimpleTag*)> styler
  );
public:

  // *********************************************************************************
  // ***** ADD CSS TO PAGE

  /// Called when the <head> is being rendered.
  /// Allows a renderer to inject a <style> tag with additional CSS.
  virtual Optional<std::string> addCss() = 0;


  // *********************************************************************************
  // ***** ADD JS TO PAGE

  /// Called once the <body> has been rendererd.
  /// Allows a renderer to inject a <script> tag with additional at the end of the body.
  virtual Optional<std::string> addJs() = 0;


  // *********************************************************************************
  // ***** RENDER TO CONTROL BAR

  struct ControlCheckbox {
    ControlCheckbox(const Twine& display_name, const Twine& css_id, bool initially_checked = true)
    : display_name{display_name.str()}
    , css_id{css_id.str()}
    , initially_checked{initially_checked}
    {}

    std::string display_name, css_id;
    bool initially_checked;
  };

  struct ControlButton {
    ControlButton(const Twine& display_name, const Twine& css_id)
    : display_name{display_name.str()}
    , css_id{css_id.str()}
    {}

    std::string display_name, css_id;
  };

  /// Called when the control bar is being rendered to checkboxes for flags
  virtual void addControlCheckboxes(VectorAppender<ControlCheckbox> dst) = 0;

  /// Called when the control bar is being rendered to add buttons
  virtual void addControlButtons(VectorAppender<ControlButton> dst) = 0;
};

/// Helper class for implementing renderers. This implementation just renders nothing
struct DummyRenderer : Renderer {
  void createRenderers(Analyses&, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {}
  void createBasicBlockStylers(Analyses&, VectorAppender<std::unique_ptr<BasicBlockStyler>> dst) override {}

  void addControlCheckboxes(VectorAppender<ControlCheckbox> dst) override {}
  void addControlButtons(VectorAppender<ControlButton> dst) override {}

  Optional<std::string> addCss() override { return None; }
  Optional<std::string> addJs() override { return None; }
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
          wrapper->add(
            analyses.names().ref(op),
            br()
          );
        }

        return wrapper;
      }
    );
  }
};

struct ScevRenderer : DummyRenderer {
  struct Visitor : SCEVVisitor<Visitor, void> {
    Visitor(ValueNameMangler& namer) : _namer{namer}, _html{div()} {}

    void visitConstant(const SCEVConstant* expr) {
      emit(_namer.ref(expr->getValue()));
    }
    void visitTruncateExpr(const SCEVTruncateExpr* expr) {
      emit("(trunc " + print(*expr->getOperand()->getType()) + " ");
      visit(expr->getOperand());
      emit(" to " + print(*expr->getOperand()->getType()) + ")");
    }
    void visitSignExtendExpr(const SCEVSignExtendExpr* expr) {
      emit("(sext " + print(*expr->getOperand()->getType()) + " ");
      visit(expr->getOperand());
      emit(" to " + print(*expr->getOperand()->getType()) + ")");
    }
    void visitZeroExtendExpr(const SCEVZeroExtendExpr* expr) {
      emit("(zext " + print(*expr->getOperand()->getType()) + " ");
      visit(expr->getOperand());
      emit(" to " + print(*expr->getOperand()->getType()) + ")");
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
        emit("sizeof(" + print(*AllocTy) + ")");
        return;
      }
      if (U->isAlignOf(AllocTy)) {
        emit("alignof(" + print(*AllocTy) + ")");
        return;
      }

      Type *CTy;
      Constant *FieldNo;
      if (U->isOffsetOf(CTy, FieldNo)) {
        emit("offsetof(" + print(*CTy) + ", ", _namer.ref(FieldNo), ")");
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

    ValueNameMangler& _namer;
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

/// render instruction metadata
struct MetadataRenderer final : DummyRenderer {
  void createRenderers(Analyses& analyses, VectorAppender<std::unique_ptr<AttributeRenderer>> dst) override {
    createRenderer(
      dst,
      [&]() {
        return html::str("Metadata");
      },
      [&](const Instruction& inst) mutable -> Html* {
        auto& inst_namer = analyses.names();
        auto& loops      = analyses.loops();
        auto& scev       = analyses.scev();

        auto& ctx = inst.getContext();

        SmallVector<std::pair<unsigned, MDNode*>, 4> mds;
        inst.getAllMetadata(mds);

        SmallVector<StringRef, 8> md_kinds;
        ctx.getMDKindNames(md_kinds);

        std::function<Html*(Metadata*)> render = [&](Metadata* md) -> Html* {
          if (!md) {
            return html::str("null");
          }
          if (auto val = dyn_cast<ValueAsMetadata>(md)) {
            return inst_namer.ref(val->getValue());
          }
          if (auto node = dyn_cast<MDNode>(md)) {
            auto elem = span();

            elem->add("{");

            for (const auto& sub : node->operands()) {
//              elem->add(print(*sub));
              elem->add(render(sub));
            }

            elem->add("}");

            return elem->withStyle(SimpleTag::FlowStyle);
          }

          auto str = cast<MDString>(md);
          return html::str(str->getString());
        };

        bool first = true;

        auto elem = div();

        for (auto md : mds) {
          if (!first)
            elem->add(br());
          first = false;

          elem->add(html::str(md_kinds[md.first] + " -> "), render(md.second));
        }

        return elem;
      }
    );
  }
};

/// style basic blocks to show loop depth as colors
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

  void addControlCheckboxes(VectorAppender<ControlCheckbox> dst) {
    dst.emplace_back("Show loop depth", "loop-depth-color", true);
  }

  Optional<std::string> addCss() override {
    std::string str;
    raw_string_ostream OS{str};

    OS << R"(
      /******************************************/
      /* color even/odd table rows differently */
      /* we also encode loop nesting in colors */)";

    for (unsigned i = 0, e = Style::maxLoopDepth(); i <= e; i++) {
      OS << "      tr.loop-" << i << "-odd  { background-color: " << Style::hardColorForLoopDepth(0).css() << "; }\n";
      OS << "\n";
      OS << "      tr.loop-" << i << "-even { background-color: " << Style::softColorForLoopDepth(0).css() << "; }\n";
    }

    OS << "\n";
    OS << "\n";

    for (unsigned i = 0, e = Style::maxLoopDepth(); i <= e; i++) {
      OS << "      body.loop-depth-color tr.loop-" << i << "-odd  { background-color: " << Style::hardColorForLoopDepth(i).css() << "; }\n";
      OS << "\n";
      OS << "      body.loop-depth-color tr.loop-" << i << "-even { background-color: " << Style::softColorForLoopDepth(i).css() << "; }\n";
    }

    OS << "\n";

    OS.flush();
    return str;
  }
};

/// Adds checkboxes for hiding the code & arguments & loop-info of functions.
struct HideCodeStyler final : DummyRenderer {
  void addControlCheckboxes(VectorAppender<ControlCheckbox> dst) {
    dst.emplace_back("Display arguments", "display-args",  true);
    dst.emplace_back("Display code",      "display-code",  true);
    dst.emplace_back("Display loops",     "display-loops", true);
  }

  Optional<std::string> addCss() override {
    return std::string{R"(
      body:not(.display-args)  .function table.arg-table   { display: none; }
      body:not(.display-code)  .function table.block-table { display: none; }
      body:not(.display-loops) .function table.loop-table  { display: none; }
    )"};
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
    _renderers.emplace_back(new ScevRenderer{});
    _renderers.emplace_back(new MetadataRenderer{});

    _renderers.emplace_back(new LoopDepthStyler{});
    _renderers.emplace_back(new HideCodeStyler{});

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
        /* collapsable code for function */

        /*
         * the collapse/expand `button' is implemented as a link,
         * disable underlining and selection to make it feel more like a button
         */
        .function-collapse-btn, .function-expand-btn {
           margin-left: 0.5em;
           text-decoration: none !important;
           -moz-user-select: none;
        }

        .function           .expander  { display: none; }
        .function.collapsed .expander  { display: inline; }
        .function.collapsed .collapser { display: none; }

        #collapse-all-btn           .expander  { display: none; }
        #collapse-all-btn.collapsed .expander  { display: inline; }
        #collapse-all-btn.collapsed .collapser { display: none; }


        /******************************************/
        /* collapsable overlay for showing CFG */

        /* The Overlay (background) */
        #cfg-overlay {
            /* Height & width depends on how you want to reveal the overlay (see JS below) */
            height: 0;
            width: 100%;
            position: fixed; /* Stay in place */
            z-index: 200;    /* Sit on top */
            left: 0;
            top: 0;
            background-color: rgb(0,0,0); /* Black fallback color */
            background-color: rgba(0,0,0, 0.9); /* Black w/opacity */
            overflow-x: hidden; /* Disable horizontal scroll */
            transition: 0.5s; /* 0.5 second transition effect to slide in or slide down the overlay (height or width, depending on reveal) */
        }

        /* Position the content inside the overlay */
        #cfg-overlay-content {
            position: relative;
            top: 25%; /* 25% from the top */
            width: 100%; /* 100% width */
            text-align: center; /* Centered text/links */
            margin-top: 30px; /* 30px top margin to avoid conflict with the close button on smaller screens */
        }

        /* The navigation links inside the overlay */
        #cfg-overlay a {
            padding: 8px;
            text-decoration: none;
            font-size: 36px;
            color: #818181;
            display: block; /* Display block instead of inline */
            transition: 0.3s; /* Transition effects on hover (color) */
        }

        /* When you mouse over the navigation links, change their color */
        #cfg-overlay a:hover, #cfg-overlay a:focus {
            color: #f1f1f1;
        }

        /* Position the close button (top right corner) */
        #cfg-overlay-closebtn {
            position: absolute;
            top:      2ex;
            right:    2em;
        }

        /*
         * When the height of the screen is less than 450 pixels, change the
         * font-size of the links and position the close button again,
         * so they don't overlap
         */
        @media screen and (max-height: 450px) {
            #cfg-overlay a {font-size: 20px}
            #cfg-overlay-closebtn {
                font-size: 40px !important;
                top: 15px;
                right: 35px;
            }
        }

        .cfg-image {
            display: none;
        }

        /******************************************/
        /* fixed position bar on top of page with checkboxes for enabling/disabling display flags */

        #control-bar {
            position: fixed;
            top: 0;
            right: 0;
            z-index: 100;
            background-color: rgb(220,220,220);
            border-radius: 0 0 0 1em;
            padding: 2em;
            width: auto;
        }

        #control-bar th {
          text-align: center;
        }
      )"));
      head->add(style(BootstrapCssSource()));

      for (auto& renderer: _renderers) {
        if (auto code = renderer->addCss()) {
          head->add(style(*code));
        }
      }

      head->add(tag("title", analyses.module().getModuleIdentifier()));

      doc->add(head);
    }

    auto body = tag("body");

    /// Render control bar which contains buttons & checkboxes for various flags & display actions
    {
      std::vector<Renderer::ControlCheckbox> checkboxes;
      std::vector<Renderer::ControlButton>   buttons;

      auto control_bar = table(css_id("control-bar"), css_class("table"));

      /// Render fold all functions button
      {
        auto fold_all = a(
          css_id("collapse-all-btn"),
          css_class("btn-link"),
          attr("href", "javascript:void(0)"),
          span(css_class("collapser"), "Collapse all functions"),
          span(css_class("expander"), "Expand all functions")
        );

        control_bar->add(
          tr(
            th(
              attr("colspan", 2),
              css_class("control-bar-control"),
              fold_all
            )
          )
        );
      }

      control_bar->add(tr());

      /// Render checkboxes for various display flags
      {
        for (auto& renderer : _renderers) {
          renderer->addControlCheckboxes(checkboxes);
          renderer->addControlButtons(buttons);
        }

        for (auto& button : buttons) {
          control_bar->add(
            tr(
              th(
                attr("colspan", 2),
                html::a(
                  css_id(button.css_id),
                  button.display_name
                )
              )
            )
          );
        }

        control_bar->add(tr());

        for (auto& flag : checkboxes) {
          auto checkbox = html::input(
            "checkbox",
            css_id(flag.css_id),
            attr("data-flag", flag.css_id)
          );

          if (flag.initially_checked) {
            body->addClass(flag.css_id);
            checkbox->addAttr("checked");
          }

          control_bar->add(
            tr(
              th(checkbox),
              th(flag.display_name)
            )
          );
        }
      }

      body->add(control_bar);
    }

    /// add overlay for displaying function CFG
    {
      /**
       * Important elements classes:
       *  - #cfg-overlay .......... container for the whole overlay the CFG image is shown here
       *  - .cfg-overlay-closer ... any link with this class closes the overlay when clicked.
       */

      auto overlay = div(
        attr("id", "cfg-overlay"),

        /// close button for the overlay
        // <a href="javascript:void(0)" class="closebtn" onclick="closeNav()">&times;</a>
        html::a(
          css_id("cfg-overlay-closebtn"),
          css_class("cfg-overlay-closer btn-large btn-link"),
          attr("href", "javascript:void(0)"),
          html::times(), "close"
        ),

        /// overlay content
        html::div(
          css_id("cfg-overlay-content"),
          new VerbatimTag("div", R"XO(
            <svg >
              <defs>
                <linearGradient id="grad1" x1="0%" y1="0%" x2="100%" y2="0%">
                  <stop offset="0%"   style="stop-color:rgb(255,255,0);stop-opacity:1" />
                  <stop offset="100%" style="stop-color:rgb(255,0,0);stop-opacity:1" />
                </linearGradient>
              </defs>
              <ellipse cx="100" cy="70" rx="85" ry="55" fill="url(#grad1)" />
                 <text fill="#ffffff" font-size="45" font-family="Verdana" x="50" y="86">
                   <a xlink:href="#_at_main2" class="cfg-overlay-closer">main2</a>
                 </text>
                 Sorry, your browser does not support inline SVG.
            </svg>
          )XO")
        )
      );

      body->add(overlay);
    }

    for (auto& fn : analyses.module()) {
      if (fn.empty())
        continue;

      auto fn_html = emitFunction(fn);

      body->add(fn_html);
    }

    body->add(script(jQuerySource()));
    body->add(script(BootstrapJsSource()));

    /// JS for enabling/disabling the display flags from checkboxes in the control-bar
    body->add(script(R"(
        $('#control-bar input:checkbox').change(function(){
          var css_class = $(this).data('flag');

          if ($(this).is(':checked')) {
            $('body').addClass(css_class);
          } else {
            $('body').removeClass(css_class);
          }
        });
    )"));

    /// JS for collapsing/expanding code for a function
    body->add(script(R"(
      $('.function-collapse-btn').click(function(){
        var button          = $(this);
        var target_selector = button.data('target');
        var target          = $(target_selector);

        target.toggleClass('collapsed');

        // TODO: find a way to do the folding/unfolding in CSS
        if (target.hasClass('collapsed')) {
          target.find('.function-code').slideUp();
        } else {
          target.find('.function-code').slideDown();
        }
      });

      // collapse all button
      $('#control-bar #collapse-all-btn').click(function(){
        $(this).toggleClass('collapsed');

        if ($(this).hasClass('collapsed')) {
          $('.function').addClass('collapsed');
          $('.function-code').slideUp();
        } else {
          $('.function').removeClass('collapsed');
          $('.function-code').slideDown();
        }
      });
    )"));

    /// JS for showing overlay with image of function CFG
    body->add(script(R"(
      $('.function-name').click(function(){
        $('#cfg-overlay').css('height', "100%");
      });

      $('.cfg-overlay-closer').click(function(){
        $('#cfg-overlay').css('height', "0%");
      });
    )"));

    body->add(new VerbatimTag("div", R"XO(
      <svg height="130" width="500">
        <defs>
          <linearGradient id="grad1" x1="0%" y1="0%" x2="100%" y2="0%">
            <stop offset="0%"   style="stop-color:rgb(255,255,0);stop-opacity:1" />
            <stop offset="100%" style="stop-color:rgb(255,0,0);stop-opacity:1" />
          </linearGradient>
        </defs>
        <ellipse cx="100" cy="70" rx="85" ry="55" fill="url(#grad1)" />
           <text fill="#ffffff" font-size="45" font-family="Verdana" x="50" y="86">
             <a xlink:href="#_at_main2" class="cfg-overlay-closer">Nyaah!</a>
           </text>
           Sorry, your browser does not support inline SVG.
      </svg>
    )XO"));

    for (auto& renderer: _renderers) {
      if (auto code = renderer->addJs()) {
        body->add(script(*code));
      }
    }

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

    auto main = html::div(
      css_class("function expanded"),
      css_id(getId(fn))
    );

    /// render header with function name & fold/unfold button
    {
      auto header = tag("h1");

      /// buttons to fold/expand code for function
      header->add(
        html::a(
          css_class("function-collapse-btn btn-link"),
          data_attr("target", '#' + getId(fn)),
          // displayed when fn is expanded
          span(css_class("collapser"), times()),
          // displayed when fn is collapsed
          span(css_class("expander"), minus())
        )
      );

      /// name of function and at the same time button to show CFG of function.
      header->add(
        span(
          css_class("function-name"),
          data_attr("target", '#' + getId(fn) + "-cfg"),
          fn.getName()
        )
      );

      main->add(header);
    }

    auto fn_html = html::div(css_class("function-code"), css_id(getId(fn) + "-code"));

    /// render table for function arguments`
    {
      auto table = html::table(css_class("table arg-table"));

      bool first = true;
      for (auto& arg : fn.args()) {
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

    body->add(
      tbody(
        tr(attr("style", "border-bottom: 1px solid #000;"))
      )
    );

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
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X{argc, argv};

  LLVMContext Context;

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

#if 0
  ModuleSlotTracker slots{&*M};
  InstructionNamer namer{slots};

  for (auto& fn : *M) {
    if (fn.isDeclaration())
      continue;

    DominatorTree domTree{fn};
    LoopInfo loops{domTree};

    outs() << "// " << fn.getName() << "\n";
    outs() << "\n";
    cfg2dot(outs(), fn, namer, loops);
    outs() << "\n";
  }
#else
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
#endif

  return 0;
}

/// **************************************
/// ***** Renderer

/// Helper class that just calls a lambda to render an attribute
struct LambdaRenderer final : Renderer::AttributeRenderer {
  LambdaRenderer(const std::function<Html*()>& header, const std::function<Html*(const Instruction&)>& attr)
    : _renderHeader{header}, _renderAttr{attr} {}

  Html* renderColumnHeader() override { return _renderHeader(); }
  Html* render(const Instruction& inst) override { return _renderAttr(inst); }
private:
  std::function<Html*()> _renderHeader;
  std::function<Html*(const Instruction&)> _renderAttr;
};

/// Helper class that just calls a lambda to style a block
struct LambdaBasicBlockStyler final : Renderer::BasicBlockStyler {
  LambdaBasicBlockStyler(const std::function<void(const BasicBlock&, SimpleTag*)>& styler)
    : _styler{styler} {}

  void style(const BasicBlock& bb, SimpleTag* tag) override {
    _styler(bb, tag);
  }
private:
  std::function<void(const BasicBlock&, SimpleTag*)> _styler;
};

void Renderer::createRenderer(
  VectorAppender<std::unique_ptr<AttributeRenderer>> dst,
  std::function<Html*()> renderHeader,
  std::function<Html*(const Instruction&)> renderAttr
) {
  dst.emplace_back(new LambdaRenderer{renderHeader, renderAttr});
}

void Renderer::createStyler(
  VectorAppender<std::unique_ptr<BasicBlockStyler>> dst,
  std::function<void(const BasicBlock&, SimpleTag*)> styler
) {
  dst.emplace_back(new LambdaBasicBlockStyler{styler});
}
