

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLine.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <memory>
#include "ASTContext.h"
#include "ASTDumper.h"
#include "ASTVisitor.h"
#include "Lexer.h"
#include "AST.h"
#include "Parser.h"
#include "Utils.h"
#include "Driver.h"

using namespace llvm;

// ================================================================
// "Library" functions that can be "extern'd" from user code.
// ================================================================

// putchard - putchar that takes a double and returns 0.
extern "C" double putchard(double X) {
    fputc((char)X, stderr);
    return 0;
}

// printd - printf that takes a double and prints it as "%f\n", returning 0.
extern "C" double printd(double X) {
    fprintf(stderr, "%f\n", X);
    return 0;
}

// ================================================================
// Module loading code.
// ================================================================
//static std::unique_ptr<Module> ParseInputIR(std::string InputFile) {
//    SMDiagnostic Err;
//    auto M = parseIRFile(InputFile, Err, getGlobalContext());
//    if (!M) {
//        Error("Problem parsing input IR");
//        return nullptr;
//    }
//
//    M->setModuleIdentifier("IR:" + InputFile);
//    return M;
//}

// ================================================================
// Main Driver code.
// ================================================================

// Command line options
cl::OptionCategory
CompilerCategory("Compiler Options", "Options for controlling the compilation process.");
static cl::opt<std::string>
InputFilename("input-file", cl::desc("File to compile (defaults to stdin)"),
              cl::init("-"), cl::value_desc("filename"), cl::cat(CompilerCategory));
static cl::alias
InputFileAlias("i", cl::desc("Alias for -input-file"), cl::aliasopt(InputFilename));
static cl::opt<bool> PrintAST ("print-ast", cl::desc("Prints out the AST to stdout"), cl::cat(CompilerCategory));


//static void handleCommandLineOptions() {
//    // Open the file to compile.
//    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
//    MemoryBuffer::getFileOrSTDIN(InputFilename);
//    if (std::error_code EC = FileOrErr.getError()) {
//        errs() << "Could not open input file '" << InputFilename
//        << "': " << EC.message() << '\n';
//        exit(2);
//    }
//    std::unique_ptr<MemoryBuffer> &File = FileOrErr.get();
//
//    // Initialize the lexer with the source
//    lexer = Lexer::Lexer(File->getBuffer().str());
//}

std::string fileContentsFromCommandLineOptions() {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr = MemoryBuffer::getFile(InputFilename);
    if (std::error_code EC = FileOrErr.getError()) {
        errs() << "Could not open input file '" << InputFilename
        << "': " << EC.message() << '\n';
        exit(2);
    }
    std::unique_ptr<MemoryBuffer> &File = FileOrErr.get();
    return File->getBuffer().str();
}

int main(int argc, char **argv) {

    // 1. Handle command line options
    llvm::cl::HideUnrelatedOptions( CompilerCategory );
    llvm::cl::ParseCommandLineOptions(argc,argv);

    // 2. Initialize ASTContext
    ASTContext astContext = ASTContext(InputFilename);

    // 3. Initialize Driver
    auto driver = Yorkie::Driver();

    //TODO: hacky for now, but prime the old style lexer.
    auto lexer = Lexer::Lexer(fileContentsFromCommandLineOptions());

    // 4. Initialize Parser
    auto parser = Parser();

    // 5. Passes
    // 5.1. Parsing pass

    driver.add(Yorkie::Pass("Lexing and parsing", [&lexer, &astContext, &parser]{
        parser.ParseTopLevel(lexer, astContext);

//        for (auto function : astContext.Functions) {
//            std::cout << function.Name << std::endl;
//        }
//        std::cout << "" << std::endl;
    }));

    // 5.2. AST Dumping pass.
    if (PrintAST) {
        driver.add(Yorkie::Pass("AST Dump", [&astContext]{
            auto astDumper = ASTDumper();
            astDumper.run(astContext);
        }));
    }

    // 6. Run all the passes
    driver.run();

    return 0;
}
