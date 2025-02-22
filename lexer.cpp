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
      LastChar = getChar();
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

//ExprAST <---> Base class for all expression nodes.
class ExprAST {
  public:
    virtual ~ExprAST() = default;
};

//NumberExprAST <---> Expression class for all numeric literals.
class NumberExprAST {
  double Val;
  public:
    NumberExprAST(double Val) : Val(Val) {}
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
        std::unique_ptr<ExprAST> RHS) : Op(Op), LHS(std::move(LHS), RHS(std::move(RHS)) {}
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

//-------------------------------------- Parser. ----------------------------------------------
static int CurTok;
static int getNextToken() {
  return CurTok = gettok();
}

// LogError - These are helper function for error handling.
std::unique_ptr<ExprAST> logError(const char *Str) {
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


static std::unique_ptr<ExprAST> ParseExpression() {

}










