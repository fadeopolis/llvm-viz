//
// Created by fader on 01.03.16.
//

#include <cstdio>
#include <string>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/PrettyStackTrace.h>

using namespace llvm;

struct Stringize {
  static struct TimeStamp final {
    constexpr TimeStamp() {}

    friend raw_ostream& operator<<(raw_ostream& OS, TimeStamp) {
      time_t    now     = time(0);
      struct tm tstruct = *localtime(&now);

      char buf[128];
      strftime(buf, sizeof(buf), "%x (%X)", &tstruct);

      return OS << buf;
    }
  } timestamp;

  struct OutputFile final {
    OutputFile(StringRef suffix, StringRef dst) {
      int fd;
      auto err = sys::fs::createUniqueFile("stringify-%%%%%%%." + suffix, fd, _tmp_path);

      if (err)
        error("Could not create temporary " + suffix + " file: " + err.message());

      _dst_path.append(dst);

      _OS.reset(new raw_fd_ostream{fd, true});
    }

    ~OutputFile() {
      _OS.reset(nullptr);

      if (_keep) {
        auto err = sys::fs::rename(_tmp_path, _dst_path);

        if (err)
          error("Could not create output file `" + _dst_path + "': " + err.message());
      } else {
        sys::fs::remove(_tmp_path);
      }
    }

    /// Rename file to it's even
    void keep() { _keep = true; }

    raw_ostream& os() { return *_OS; }
  private:
    bool _keep = false;
    SmallString<32> _tmp_path, _dst_path;
    std::unique_ptr<raw_fd_ostream> _OS;
  };

  static int main(int argc, char** argv) {
    cl::opt<std::string> Name("name",
                              cl::Required,
                              cl::desc("Name of function that returns the generated string literal"),
                              cl::value_desc("NAME"));

    cl::opt<std::string> Description("desc",
                                     cl::Required,
                                     cl::desc("Short Description of file we are turning into a string"),
                                     cl::value_desc("DESCRIPTION"));

    cl::opt<std::string> Input("input",
                               cl::Required,
                               cl::desc("File to turn into a C++ string literal"),
                               cl::value_desc("INPUT"));

    cl::opt<std::string> Hpp("hpp",
                             cl::Required,
                             cl::desc("Name of header file to generate"),
                             cl::value_desc("HPP"));

    cl::opt<std::string> Cpp("cpp",
                             cl::Required,
                             cl::desc("Name of cpp file to generate"),
                             cl::value_desc("CPP"));


    cl::ParseCommandLineOptions(argc, argv, "convert text to C++ string literal\n");

    checkNotEmpty(Name);
    checkNotEmpty(Description);
    checkNotEmpty(Hpp);
    checkNotEmpty(Cpp);

    for (char c : Name)
      if (!isValidVarnameChar(c))
        error("Invalid character '" + Twine{c} + "' in variable name.");

    OutputFile HppTmp{"hpp", Hpp};
    OutputFile CppTmp{"cpp", Cpp};

    FILE* input = fopen(Input.c_str(), "r");

    if (!input) {
      std::error_code err{errno, std::system_category()};
      error("Could not open input file `" + Input + "': " + err.message());
    }

    /// ***** EMIT HEADER
    {
      auto& OS = HppTmp.os();

      OS << "//\n"
      << "// Created by stringify at " << timestamp << "\n"
      << "//\n"
      << "\n"
      << "#pragma once\n"
      << "\n"
      << "#include <llvm/ADT/StringRef.h>\n"
      << "\n"
      << "namespace html {\n"
      << "\n"
      << "using namespace llvm;\n"
      << "\n"
      << "/// Returns " << Description << "as a string.\n"
      << "extern StringRef " << Name << "();\n"
      << "\n"
      << "} // end namespace html\n"
      << "\n";
      OS.flush();
    }

    /// ***** EMIT IMPLEMENTATION
    {
      auto& OS = CppTmp.os();

      OS << "//\n"
      << "// Created by stringify at " << timestamp << "\n"
      << "//\n"
      << "\n"
      << "#include \"" << Hpp << "\"\n"
      << "\n"
      << "using namespace html;\n"
      << "using namespace llvm;\n"
      << "\n"
      << "llvm::StringRef html::" << Name << "() {\n"
      << "  static const char src[] =\n";

      SmallString<256> line;
      while (getline(line, input)) {
        OS << "    \"";

        for (char c : line)
          escape(c, OS);

        OS << "\"\n";

        line.clear();
      }

      OS << "  ;\n"
         << "  return src;\n"
         << "}\n"
         << "\n";

      OS.flush();
    }

    HppTmp.keep();
    CppTmp.keep();
    return 0;
  }

  static void checkNotEmpty(cl::opt<std::string>& opt) {
    if (opt.empty())
      error("Empty string passed to option `-" + opt.ArgStr + "'");
  }

  static bool getline(SmallString<256>&dst, FILE* file) {
    char buf[256];

    while (true) {
      auto ret = ::fgets(buf, sizeof(buf), file);

      if (ret) {
        StringRef str = buf;
        dst.append(str);

        if (str.back() == '\n')
          return true;
      } else {
        if (feof(file)) {
          dst.append(buf);
          return false;
        } else {
          std::error_code err{ferror(file), std::system_category()};
          error("Could not read input file: " + err.message());
        }
      }
    }
  }

  static bool isBetween(char c, char lo, char hi) {
    return (c >= lo) && (c <= hi);
  }

  static bool isValidVarnameChar(char c) {
    return isBetween(c, '0', '9') || isBetween(c, 'a', 'z') || isBetween(c, 'A', 'Z') || (c == '_');
  }

  static raw_ostream& escape(char c, raw_ostream& OS) {
    switch (c) {
      case '\a': return OS << "\\a";
      case '\b': return OS << "\\b";
      case '\f': return OS << "\\f";
      case '\n': return OS << "\\n";
      case '\r': return OS << "\\r";
      case '\t': return OS << "\\t";
      case '\v': return OS << "\\v";
      case '\\': return OS << "\\\\";
      case '\"': return OS << "\\\"";
      default:
        if (isprint(c))
          return OS << c;

        OS << "\\x";
        OS.write_hex(c);
        return OS;
    }
  }

  static void error[[noreturn]](const Twine& msg) {
    errs().changeColor(raw_ostream::RED, true);
    errs() << "error: ";
    errs().resetColor();
    errs() << msg << "\n";
    exit(1);
  }
};

int main(int argc, char** argv) {
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X{argc, argv};

  return Stringize::main(argc, argv);
}
