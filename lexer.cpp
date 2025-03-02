#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

//---------------------------------------- Lexer -------------------------------------------------

enum Token {
  tok_eof = -1,

  //commands 
  tok_def = -2,
  tok_extern = -3,

  //primary
  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr; // filled in if tok_identifier
static double NumVal;             // filled in if tok_number.

// gettok - Return the next token from tandard input.
static int gettok() {
  static int LastChar = ' ';
  
  while (isspace(LastChar)) // skip whitespace.
    LastChar = getchar();

  if (isalpha(LastChar)) {  // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum(LastChar = getchar()))
      IdentifierStr += LastChar;
    
    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    
    return tok_identifier;
  }
  if (isdigit(LastChar) || LastChar == '.') { // number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }
  if (LastChar == '#') {  // Comment until end of line 
    do 
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }
  if (LastChar == EOF)
    return tok_eof;

  // otherwise, just return the character as its ascii value.
  int thisChar = LastChar;
  LastChar = getchar();
  return thisChar;
}

//------------------------------------------- Parse Tree.---------------------------------------

namespace {
//ExprAST <---> Base class for all expression nodes.
class ExprAST {
  public:
    virtual ~ExprAST() = default;
    virtual Value *codegen() = 0;
};

//NumberExprAST <---> Expression class for all numeric literals.
class NumberExprAST : public ExprAST {
  double Val;
  public:
    NumberExprAST(double Val) : Val(Val) {}
    Value *codegen() override;
};   

class VariableExprAST : public ExprAST { // Expression class for  referencing 
                                         // a variable. 
  std::string Name;
  public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
};

class BinaryExprAST : public ExprAST { // expression class for a binary operator.
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

  public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, 
        std::unique_ptr<ExprAST> RHS) : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};


class CallExprAST : public ExprAST { // expression class for function calls.
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

  public:
    CallExprAST(const std::string &Callee, 
        std::vector<std::unique_ptr<ExprAST>> Args) : Callee(Callee), Args(std::move(Args)) {}
};

class PrototypeAST { // prototype of a function captures it's name and arguments.
  std::string Name;
  std::vector<std::string> Args;

  public:
    PrototypeAST(const std::string &Name, 
        std::vector<std::string> Args) : Name(Name), Args(std::move(Args)) {}

    const std::string &getName() const { return Name; }
};

class FunctionAST { // This class represents a function definition itself.
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

  public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
        std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {} 
};
}

//-------------------------------------- Parser. ----------------------------------------------
static int CurTok;
static std::unique_ptr<ExprAST> ParseExpression();
static int getNextToken() {
  return CurTok = gettok();
}

// LogError - These are helper function for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(result);
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken();
  auto V = ParseExpression();
  if (!V)
    return nullptr;
  if (CurTok != ')')
    return LogError("expected ')'");
  getNextToken();
  return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;

  getNextToken(); //consume the identifier string. 
  if (CurTok != '(')
    return std::make_unique<VariableExprAST>(IdName);

  //get the next token.
  getNextToken(); // consume (
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')')
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;
      
      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }

  //eat ')' token.
  getNextToken();
  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParsePrimary() {
  switch(CurTok) {
    default:
      return LogError("unknown token when expecting an expression.");
    case tok_identifier:
      return ParseIdentifierExpr();
    case tok_number:
      return ParseNumberExpr();
    case '(':
      return ParseParenExpr();
  }
}
//precedence table.
static std::map<char, int> BinopPrecedence;

static int GetTokPrecedence()
{
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, 
    std::unique_ptr<ExprAST> LHS)
{
  while (true) {
    //if this is a binop, find its precedence.
    int TokPrec = GetTokPrecedence();

    //if this is a binop that binds atleast as tightly as the current binop.
    //consume it, otherwise done.
    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;
    getNextToken();    // consume binop.

    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    int nextPrec = GetTokPrecedence();
    if (TokPrec < nextPrec) {
      RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS)); //builds on all binary operators with higher precedence.
      if (!RHS)
        return nullptr;
    }
    //merge LHS/RHS. 
    LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}


static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype.");

  std::string fnName = IdentifierStr; // function name. 
  getNextToken();   

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype.");

  //read the list of argument names;
  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype.");

  getNextToken(); // consume ')'.
  return std::make_unique<PrototypeAST>(fnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken();
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken();        
  return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() 
{
  if (auto E = ParseExpression()) {
    auto proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(proto), std::move(E));
  }
  return nullptr;
}

static void HandleDefinition()
{
  if (ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern()
{
  if (ParseExtern()) {
    fprintf(stderr, "Parsed an extern\n");
  } else {
    // skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression()
{
  if (ParseTopLevelExpr()) {
    fprintf(stderr, "Parsed a top-level expr\n");
  } else {
    //skip token for error recovery.
    getNextToken();
  }
}

static void MainLoop() 
{
  while (true) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
      case tok_eof:
        return;
      case ';': //ignore top-level semicolons.
        getNextToken();
        break;
      case tok_def:
        HandleDefinition();
        break;
      case tok_extern:
        HandleExtern();
        break;
      default:
        HandleTopLevelExpression();
        break;
    }
  }
}

// ---------------------------- Code Generation. ---------------------------------
static std::unique_ptr<LLVMContext> Context; // contains alot of core LLVM data structures.
static std::unique_ptr<IRBuilder<>> Builder; // makes it easy to generate LLVM instructions.
static std::unique_ptr<Module> TheModule;    // contains functions and global variables.
static std::map<std::stirng, Value *> NamedValues; // keeps track of which values are defined in the current scope and what their llvm representation is.

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

Value *NumberExprAST::codegen() {
  return ConstandFP::get(*Context, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  Value *V = NamedValues[Name];
  if (!V)
    LogErrorV("Unknown variable name");
  return V;
}

Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();

  if (!L || !R)
    return nullptr;

  switch (Op) {
    case '+':
      return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
      return Builder->CreateFSub(L, R, "subtmp");
    case '*':
      return Builder->CreateFMul(L, R, "multmp");
    case '<':
      L = Builder->CreateFCmpULT(L, R, "cmptmp");
      //convert bool 0/1 to double 0.0 or 1.0 
      return Builder->CreateUIToFP(L, Type::getDoubleTy(*Context), "booltmp");
    default:
      return LogErrorV("invalid binary operator");
  }
}
// -------------------------------------------------------------------------------


int main() {
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();
  return 0;
}








