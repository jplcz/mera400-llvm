//===-- Mera400AsmParser.cpp - MERA-400 assembly parser -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Parses MERA-400 assembly. The syntax follows EMAS, the machine's original
// assembler, so expression operators, directives (.word, .res, .equ, .struct,
// .org, ...), character escapes and ".local" labels behave as EMAS programmers
// expect. Operands use the machine's "normal argument" forms: a register, a
// register plus index register, a value, or any of these in [brackets] for
// indirect addressing.
//
// Symbols denote byte addresses in ELF but instructions take word addresses,
// so symbolic values are wrapped in Mera400HalfExpr (see wordsOf).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Mera400FloatFormat.h"
#include "MCTargetDesc/Mera400MCTargetDesc.h"
#include "TargetInfo/Mera400TargetInfo.h"
#include "MCTargetDesc/Mera400MCExpr.h"
#include "MCTargetDesc/Mera400TargetStreamer.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

using namespace llvm;

namespace {
// One parsed operand: an instruction name token, a register, an immediate
// expression, or a "normal argument" (Norm) with optional indirection, index
// register and extra word.
class Mera400Operand : public MCParsedAsmOperand {
  enum Kind { Token, Reg, Imm, Norm } K;
  StringRef Tok;
  MCRegister Register;
  const MCExpr *Expr = nullptr;
  // Norm operands: Register is register C, IndexReg is register B and Expr
  // (if any) is the extra word.
  MCRegister IndexReg;
  bool Indirect = false;
  SMLoc Start, End;

public:
  Mera400Operand(StringRef Tok, SMLoc Loc)
      : K(Token), Tok(Tok), Start(Loc), End(Loc) {}
  Mera400Operand(MCRegister Register, SMLoc Start, SMLoc End)
      : K(Reg), Register(Register), Start(Start), End(End) {}
  Mera400Operand(const MCExpr *Expr, SMLoc Start, SMLoc End)
      : K(Imm), Expr(Expr), Start(Start), End(End) {}

  static std::unique_ptr<Mera400Operand>
  createNorm(bool Indirect, MCRegister RegC, MCRegister RegB,
             const MCExpr *Expr, SMLoc Start, SMLoc End) {
    auto Op = std::make_unique<Mera400Operand>(RegC, Start, End);
    Op->K = Norm;
    Op->IndexReg = RegB;
    Op->Indirect = Indirect;
    Op->Expr = Expr;
    return Op;
  }

  bool isToken() const override { return K == Token; }
  bool isReg() const override { return K == Reg; }
  bool isImm() const override { return K == Imm; }
  bool isMem() const override { return false; }
  MCRegister getReg() const override { return Register; }
  StringRef getToken() const { return Tok; }
  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  bool isShortImm() const {
    int64_t Value;
    return isImm() && Expr->evaluateAsAbsolute(Value) && Value >= -63 &&
           Value <= 63;
  }
  // A short value: a 7-bit literal or a label resolved by a fixup.
  bool isShortVal() const { return isShortBr(); }
  // A branch target: a 7-bit literal or a label resolved by a fixup.
  bool isShortBr() const {
    int64_t Value;
    if (!isImm())
      return false;
    return !Expr->evaluateAsAbsolute(Value) || (Value >= -63 && Value <= 63);
  }
  // A normal argument with no extra word: register C, optionally indexed.
  bool isNormReg() const { return K == Reg || (K == Norm && !Expr); }
  // A normal argument whose extra word fits in 16 bits (signed or unsigned).
  bool isNormExt() const {
    const MCExpr *E = getExtraExpr();
    if (!E)
      return false;
    int64_t Value;
    if (!E->evaluateAsAbsolute(Value))
      return true;
    return Value >= -32768 && Value <= 65535;
  }
  const MCExpr *getExtraExpr() const {
    return K == Imm || (K == Norm && Expr) ? Expr : nullptr;
  }
  unsigned controlBits(bool WithRegC) const {
    auto Encoding = [](MCRegister R) { return R.id() - Mera400::R0; };
    unsigned Control = 0;
    if (K == Reg)
      return Encoding(Register);
    if (Indirect)
      Control |= 0x40;
    if (IndexReg)
      Control |= Encoding(IndexReg) << 3;
    if (WithRegC && Register)
      Control |= Encoding(Register);
    return Control;
  }
  template <int64_t Lo, int64_t Hi> bool isImmRange() const {
    int64_t Value;
    return isImm() && Expr->evaluateAsAbsolute(Value) && Value >= Lo &&
           Value <= Hi;
  }
  bool isLeftByteImm() const {
    int64_t Value;
    return isImm() && Expr->evaluateAsAbsolute(Value) && Value >= 0 &&
           (Value & ~0xff00) == 0;
  }
  void addNormRegOperands(MCInst &Inst, unsigned) const {
    Inst.addOperand(MCOperand::createImm(controlBits(true)));
  }
  void addNormExtOperands(MCInst &Inst, unsigned) const {
    Inst.addOperand(MCOperand::createImm(controlBits(false)));
    int64_t Value;
    if (Expr->evaluateAsAbsolute(Value))
      Inst.addOperand(MCOperand::createImm(Value));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }
  void addShortBrOperands(MCInst &Inst, unsigned) const {
    int64_t Value;
    if (Expr->evaluateAsAbsolute(Value))
      Inst.addOperand(MCOperand::createImm(Value));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }
  void addRegOperands(MCInst &Inst, unsigned) const {
    Inst.addOperand(MCOperand::createReg(Register));
  }
  void addImmOperands(MCInst &Inst, unsigned) const {
    int64_t Value;
    bool IsAbsolute = Expr->evaluateAsAbsolute(Value);
    assert(IsAbsolute && "short immediate must be absolute");
    (void)IsAbsolute;
    Inst.addOperand(MCOperand::createImm(Value));
  }
  void print(raw_ostream &OS, const MCAsmInfo &) const override {
    if (isToken())
      OS << Tok;
    else if (isReg())
      OS << "register " << Register.id();
    else if (K == Norm)
      OS << "normal argument";
    else
      OS << "immediate";
  }
};

// Result of an EMAS constant expression: EMAS expressions are either 64-bit
// integers or doubles, and only .float accepts the latter.
struct EmasValue {
  bool IsFloat = false;
  int64_t I = 0;
  double D = 0;
  // Set when the value involves symbols. Symbol values are in bytes (the ELF
  // convention) with constants already scaled; the word value is Sym / 2.
  const MCExpr *Sym = nullptr;
  // A symbolic value that is a distance (address minus address), not an address.
  bool Plain = false;
  // Sym is already a finished word value (hi() or lo()), not a byte address.
  bool Wrapped = false;
  MCRegister Reg; // a "+ rB" index register found in an operand
  double asDouble() const { return IsFloat ? D : static_cast<double>(I); }
};

// The parser keeps its own constant table and expression evaluator (EMAS
// semantics); MCTargetAsmParser supplies matching of the TableGen-generated
// instruction patterns.
class Mera400AsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

#define GET_ASSEMBLER_HEADER
#include "Mera400GenAsmMatcher.inc"

  bool matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  bool parseRegister(MCRegister &Reg, SMLoc &Start, SMLoc &End) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &Start,
                               SMLoc &End) override;
  bool parseInstruction(ParseInstructionInfo &, StringRef Name, SMLoc NameLoc,
                        OperandVector &Operands) override;
  bool parseNormArg(OperandVector &Operands, bool Indirect);
  bool parseNormValue(OperandVector &Operands, bool Indirect, SMLoc Start);
  ParseStatus parseDirective(AsmToken DirectiveID) override;
  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;
  bool isLabel(AsmToken &Token) override {
    // Local labels and struct fields are handled by this parser.
    return !InStruct && !Token.getString().starts_with(".");
  }
  void onLabelParsed(MCSymbol *Symbol) override {
    GlobalLabel = Symbol->getName().str();
  }
  bool parseEmasExpr(EmasValue &Val, unsigned Level = 0);
  bool parseEmasUnary(EmasValue &Val);
  bool parseEmasInt(int64_t &Val, const char *What);
  bool parseOperandExpr(const MCExpr *&Expr, MCRegister *Index = nullptr);
  bool resolveIdentifier(StringRef Name, SMLoc Loc, EmasValue &Val);
  bool defineConstant(StringRef Name, const EmasValue &Val, bool Redefinable,
                      SMLoc Loc);
  bool parseEquDirective(bool Redefinable);
  bool parseStructDirective();
  bool parseEndStructDirective();
  bool parseCpuDirective();
  bool parseModelDirective();
  bool ModelSet = false, ModelLarge = false;
  bool parseLocalLabel(StringRef Name, SMLoc Loc);
  bool parseStructField(StringRef Name, SMLoc Loc);
  const MCExpr *wordsOf(const EmasValue &Val);

  // Named constants (.equ, .const, struct sizes and fields), kept by value so
  // later redefinitions do not affect expressions already parsed.
  StringMap<EmasValue> Constants;
  StringSet<> FixedConstants;
  std::string GlobalLabel; // scope of local labels such as ".loop"
  bool InStruct = false;
  std::string StructName, SavedGlobalLabel;
  int64_t StructOffset = 0;
  bool Mx16 = false;
  bool CpuSet = false;
  bool StructFieldParsed = false;
  bool parseEndOfDirective();
  bool parseWordDirective();
  bool parseDwordDirective();
  bool parseFloatDirective();
  bool parseStringDirective(bool Terminate);
  bool parseResDirective();
  bool parseOrgDirective();

public:
  Mera400AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    Parser.getLexer().setLexQuotedCharLiterals(true);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};
} // namespace

static MCRegister MatchRegisterName(StringRef Name);

ParseStatus Mera400AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &Start,
                                               SMLoc &End) {
  if (Parser.getTok().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;
  Reg = MatchRegisterName(Parser.getTok().getIdentifier().lower());
  if (!Reg)
    return ParseStatus::NoMatch;
  Start = Parser.getTok().getLoc();
  End = Parser.getTok().getEndLoc();
  Parser.Lex();
  return ParseStatus::Success;
}

bool Mera400AsmParser::parseRegister(MCRegister &Reg, SMLoc &Start,
                                     SMLoc &End) {
  if (tryParseRegister(Reg, Start, End).isSuccess())
    return false;
  return Error(Parser.getTok().getLoc(), "expected MERA-400 register");
}

// EMAS expression operators bind differently from LLVM's, so constant
// expressions are parsed here instead of with MCAsmParser::parseExpression.
// Levels from loosest to tightest: |  ^  &  >>  <<  + -  * / %  \ (scale).
static bool emasLevelOf(const AsmToken &Tok, unsigned Level) {
  static const AsmToken::TokenKind Kinds[][3] = {
      {AsmToken::Pipe, AsmToken::Eof, AsmToken::Eof},
      {AsmToken::Caret, AsmToken::Eof, AsmToken::Eof},
      {AsmToken::Amp, AsmToken::Eof, AsmToken::Eof},
      {AsmToken::GreaterGreater, AsmToken::Eof, AsmToken::Eof},
      {AsmToken::LessLess, AsmToken::Eof, AsmToken::Eof},
      {AsmToken::Plus, AsmToken::Minus, AsmToken::Eof},
      {AsmToken::Star, AsmToken::Slash, AsmToken::Percent},
      {AsmToken::BackSlash, AsmToken::Eof, AsmToken::Eof}};
  for (AsmToken::TokenKind K : Kinds[Level])
    if (K != AsmToken::Eof && Tok.is(K))
      return true;
  return false;
}

static constexpr unsigned EmasLevels = 8;

// Decodes one EMAS escape sequence or plain character at Body[Pos] and
// advances Pos. Returns false with Err set for invalid escapes.
static bool decodeEmasChar(StringRef Body, size_t &Pos, unsigned &Value,
                           std::string &Err) {
  if (Body[Pos] != '\\') {
    Value = static_cast<unsigned char>(Body[Pos++]);
    return true;
  }
  size_t Start = Pos;
  if (Pos + 1 >= Body.size()) {
    Err = "Invalid escape sequence: \"\\\"";
    return false;
  }
  char C = Body[Pos + 1];
  size_t Len = 2;
  switch (C) {
  case 'a': Value = '\a'; break;
  case 'b': Value = '\b'; break;
  case 'f': Value = '\f'; break;
  case 'n': Value = '\n'; break;
  case 'r': Value = '\r'; break;
  case 't': Value = '\t'; break;
  case 'v': Value = '\v'; break;
  case '\\': Value = '\\'; break;
  case '"': Value = '"'; break;
  case '\'': Value = '\''; break;
  case 'x': {
    if (Pos + 3 >= Body.size() ||
        !isHexDigit(Body[Pos + 2]) || !isHexDigit(Body[Pos + 3])) {
      Err = "Invalid escape sequence: \"" + Body.substr(Pos, 4).str() + "\"";
      return false;
    }
    Value = hexDigitValue(Body[Pos + 2]) * 16 + hexDigitValue(Body[Pos + 3]);
    Len = 4;
    break;
  }
  default:
    if (C >= '0' && C <= '7') {
      Value = 0;
      Len = 1;
      while (Len < 4 && Pos + Len < Body.size() && Body[Pos + Len] >= '0' &&
             Body[Pos + Len] <= '7')
        Value = Value * 8 + (Body[Pos + Len++] - '0');
      if (Value > 255) {
        Err = "Invalid escape sequence (value too big): \"" +
              Body.substr(Start, Len).str() + "\"";
        return false;
      }
      break;
    }
    Err = "Invalid escape sequence: \"" + Body.substr(Pos, 2).str() + "\"";
    return false;
  }
  Pos += Len;
  return true;
}

static int emasFlagMask(char C) {
  static const StringRef Flags = "ZMVCLEGYX1234567";
  size_t Idx = Flags.find(C);
  return Idx == StringRef::npos ? -1 : 1 << (15 - Idx);
}

bool Mera400AsmParser::parseEmasUnary(EmasValue &Val) {
  AsmToken Tok = Parser.getTok();
  SMLoc Loc = Tok.getLoc();
  Val = EmasValue();
  if (Tok.is(AsmToken::Minus) || Tok.is(AsmToken::Tilde)) {
    Parser.Lex();
    SMLoc OperandLoc = Parser.getTok().getLoc();
    if (parseEmasUnary(Val))
      return true;
    if (Val.Sym) {
      if (Val.Wrapped)
        return Error(OperandLoc, "hi() and lo() cannot be used in an "
                                 "expression");
      if (Tok.is(AsmToken::Tilde))
        return Error(OperandLoc, "operator '~' requires a constant expression");
      Val.Sym = MCUnaryExpr::createMinus(Val.Sym, getContext());
    } else if (Tok.is(AsmToken::Minus)) {
      Val.I = -Val.I;
      Val.D = -Val.D;
    } else if (Val.IsFloat) {
      return Error(OperandLoc, "operator '~' requires an integer expression");
    } else {
      Val.I = ~Val.I;
    }
    return false;
  }
  if (Tok.is(AsmToken::LParen)) {
    Parser.Lex();
    if (parseEmasExpr(Val))
      return true;
    if (Parser.getTok().isNot(AsmToken::RParen))
      return Error(Parser.getTok().getLoc(), "expected ')'");
    Parser.Lex();
    return false;
  }
  if (Tok.is(AsmToken::Integer) || Tok.is(AsmToken::BigNum)) {
    // The lexer stops at '_', so glue adjacent "_digits" identifiers back on.
    std::string Text = Tok.getString().str();
    const char *End = Tok.getEndLoc().getPointer();
    Parser.Lex();
    while (Parser.getTok().is(AsmToken::Identifier) &&
           Parser.getTok().getLoc().getPointer() == End &&
           Parser.getTok().getString().starts_with("_")) {
      Text += Parser.getTok().getString();
      End = Parser.getTok().getEndLoc().getPointer();
      Parser.Lex();
    }
    llvm::erase(Text, '_');
    uint64_t U;
    if (StringRef(Text).getAsInteger(0, U) || U > INT64_MAX)
      return Error(Loc, "Integer conversion error");
    Val.I = U;
    return false;
  }
  // hi(expr) and lo(expr): the high and low word of a byte address. The
  // argument counts bytes, unlike the rest of an EMAS expression.
  if (Tok.is(AsmToken::Identifier) &&
      (Tok.getString() == "hi" || Tok.getString() == "lo") &&
      getLexer().peekTok().is(AsmToken::LParen)) {
    bool High = Tok.getString() == "hi";
    Parser.Lex();
    Parser.Lex();
    const MCExpr *Inner;
    if (Parser.parseExpression(Inner))
      return true;
    if (Parser.getTok().isNot(AsmToken::RParen))
      return Error(Parser.getTok().getLoc(), "expected ')'");
    Parser.Lex();
    int64_t Bytes;
    if (Inner->evaluateAsAbsolute(Bytes)) {
      Val.I = (High ? Bytes >> 16 : Bytes) & 0xffff;
      return false;
    }
    Val.Sym = Mera400HalfExpr::create(
        Inner, getContext(), false,
        High ? Mera400HalfExpr::Hi : Mera400HalfExpr::Lo);
    Val.Wrapped = true;
    return false;
  }
  if (Tok.is(AsmToken::Identifier) || Tok.is(AsmToken::Dot)) {
    Parser.Lex();
    return resolveIdentifier(Tok.is(AsmToken::Dot) ? "." : Tok.getString(),
                             Loc, Val);
  }
  if (Tok.is(AsmToken::Real)) {
    Parser.Lex();
    Val.IsFloat = true;
    if (Tok.getString().getAsDouble(Val.D))
      return Error(Loc, "Invalid floating point number");
    return false;
  }
  if (Tok.is(AsmToken::Question)) {
    // Flag literal such as ?ZE: the lexer splits it into adjacent tokens.
    std::string Text;
    const char *End = Tok.getEndLoc().getPointer();
    Parser.Lex();
    while ((Parser.getTok().is(AsmToken::Identifier) ||
            Parser.getTok().is(AsmToken::Integer)) &&
           Parser.getTok().getLoc().getPointer() == End) {
      Text += Parser.getTok().getString();
      End = Parser.getTok().getEndLoc().getPointer();
      Parser.Lex();
    }
    if (Text.empty())
      return Error(Loc, "expected flags after '?'");
    for (char C : Text) {
      int Mask = emasFlagMask(C);
      if (Mask < 0)
        return Error(Loc, "Unknown flag: '" + Twine(C) + "'");
      if (Val.I & Mask)
        return Error(Loc, "Duplicated flag: '" + Twine(C) + "'");
      Val.I |= Mask;
    }
    return false;
  }
  if (Tok.is(AsmToken::String) && Tok.getString().starts_with("'")) {
    StringRef Body = Tok.getString().drop_front().drop_back();
    Parser.Lex();
    size_t Pos = 0;
    unsigned Chars[2], Count = 0;
    std::string Err;
    while (Pos < Body.size() && Count < 2)
      if (!decodeEmasChar(Body, Pos, Chars[Count++], Err))
        return Error(Loc, Err);
    if (Count == 0 || Pos != Body.size())
      return Error(Loc, "Invalid character literal: " + Tok.getString());
    Val.I = Count == 1 ? Chars[0] : Chars[0] * 256 + Chars[1];
    return false;
  }
  if (Tok.is(AsmToken::Error) && !Tok.getString().empty() &&
      isDigit(Tok.getString()[0]))
    return Error(Loc, "Integer conversion error");
  return Error(Loc, "expected expression");
}

bool Mera400AsmParser::parseEmasExpr(EmasValue &Val, unsigned Level) {
  if (Level == EmasLevels)
    return parseEmasUnary(Val);
  if (parseEmasExpr(Val, Level + 1))
    return true;
  while (emasLevelOf(Parser.getTok(), Level)) {
    AsmToken Op = Parser.getTok();
    Parser.Lex();
    SMLoc RhsLoc = Parser.getTok().getLoc();
    EmasValue Rhs;
    if (parseEmasExpr(Rhs, Level + 1))
      return true;
    if (Rhs.Reg || Val.Reg) {
      if (!Op.is(AsmToken::Plus) || (Rhs.Reg && Val.Reg))
        return Error(RhsLoc, "a register is only allowed as 'value + register'");
      if (Rhs.Reg)
        Val.Reg = Rhs.Reg;
      Rhs.Reg = MCRegister();
      if (!Val.Sym && !Rhs.Sym && !Val.IsFloat && !Rhs.IsFloat) {
        Val.I += Rhs.I;
        continue;
      }
    }
    if (Val.Sym || Rhs.Sym) {
      if (Val.Wrapped || Rhs.Wrapped)
        return Error(RhsLoc, "hi() and lo() cannot be used in an expression");
      if (Val.IsFloat || Rhs.IsFloat || !(Op.is(AsmToken::Plus) || Op.is(AsmToken::Minus)))
        return Error(RhsLoc, "operator '" + Op.getString() +
                                 "' requires constant expressions");
      const MCExpr *L = Val.Sym ? Val.Sym : MCConstantExpr::create(Val.I * 2, getContext());
      const MCExpr *R = Rhs.Sym ? Rhs.Sym : MCConstantExpr::create(Rhs.I * 2, getContext());
      // As in EMAS, address minus address is a plain number.
      bool LAddr = Val.Sym && !Val.Plain, RAddr = Rhs.Sym && !Rhs.Plain;
      bool Addr = Op.is(AsmToken::Minus) && LAddr && RAddr ? false
                                                           : LAddr || RAddr;
      Val.Plain = !Addr;
      Val.Sym = MCBinaryExpr::create(Op.is(AsmToken::Plus) ? MCBinaryExpr::Add : MCBinaryExpr::Sub, L, R, getContext());
      continue;
    }
    if (Val.IsFloat || Rhs.IsFloat) {
      bool Arithmetic = Op.is(AsmToken::Plus) || Op.is(AsmToken::Minus) ||
                        Op.is(AsmToken::Star) || Op.is(AsmToken::Slash);
      if (!Arithmetic)
        return Error(RhsLoc, "operator '" + Op.getString() +
                                 "' requires integer expressions");
      double L = Val.asDouble(), R = Rhs.asDouble();
      if (Op.is(AsmToken::Slash) && R == 0)
        return Error(RhsLoc, "Division by 0");
      Val.IsFloat = true;
      Val.D = Op.is(AsmToken::Plus)    ? L + R
              : Op.is(AsmToken::Minus) ? L - R
              : Op.is(AsmToken::Star)  ? L * R
                                       : L / R;
      continue;
    }
    switch (Op.getKind()) {
    case AsmToken::Pipe: Val.I |= Rhs.I; break;
    case AsmToken::Caret: Val.I ^= Rhs.I; break;
    case AsmToken::Amp: Val.I &= Rhs.I; break;
    case AsmToken::Plus: Val.I += Rhs.I; break;
    case AsmToken::Minus: Val.I -= Rhs.I; break;
    case AsmToken::Star: Val.I *= Rhs.I; break;
    case AsmToken::Slash:
    case AsmToken::Percent:
      if (Rhs.I == 0)
        return Error(RhsLoc, "Division by 0");
      Val.I = Op.is(AsmToken::Slash) ? Val.I / Rhs.I : Val.I % Rhs.I;
      break;
    default: // shifts and scale
      if (Rhs.I < 0 || Rhs.I > 15)
        return Error(RhsLoc, Twine(Op.is(AsmToken::BackSlash)
                                       ? "scale amount "
                                       : "shift amount ") +
                                 Twine(Rhs.I) + " is out of range [0, 15]");
      if (Op.is(AsmToken::GreaterGreater))
        Val.I >>= Rhs.I;
      else if (Op.is(AsmToken::LessLess))
        Val.I = (uint64_t)Val.I << Rhs.I;
      else
        Val.I = (uint64_t)Val.I << (15 - Rhs.I);
    }
  }
  return false;
}

// Parses an expression that must be an integer; What names the context for
// the diagnostic.
bool Mera400AsmParser::parseEmasInt(int64_t &Val, const char *What) {
  SMLoc Loc = Parser.getTok().getLoc();
  EmasValue V;
  if (parseEmasExpr(V))
    return true;
  if (V.Sym || V.Reg)
    return Error(Loc, Twine(What) + " requires a constant expression");
  if (V.IsFloat)
    return Error(Loc, Twine(What) + " requires an integer expression, got float (" +
                          Twine(formatv("{0}", V.D).str()) + ")");
  Val = V.I;
  return false;
}

bool Mera400AsmParser::parseEndOfDirective() {
  if (Parser.getTok().isNot(AsmToken::EndOfStatement))
    return Error(Parser.getTok().getLoc(), "unexpected token in directive");
  Parser.Lex();
  return false;
}

// A symbolic value is a byte address, but data words and instruction operands
// hold word addresses, so it is halved; a finished hi()/lo() value is kept as
// is, and a plain constant needs no conversion.
const MCExpr *Mera400AsmParser::wordsOf(const EmasValue &Val) {
  if (Val.Wrapped)
    return Val.Sym;
  if (Val.Sym)
    return Mera400HalfExpr::create(Val.Sym, getContext(), Val.Plain);
  return MCConstantExpr::create(Val.I, getContext());
}

bool Mera400AsmParser::parseWordDirective() {
  while (true) {
    SMLoc Loc = Parser.getTok().getLoc();
    EmasValue V;
    if (parseEmasExpr(V))
      return true;
    if (V.Reg)
      return Error(Loc, "instruction operand or .word requires a constant "
                        "expression");
    if (V.IsFloat)
      return Error(Loc, "instruction operand or .word requires an integer "
                        "expression, got float (" +
                            Twine(formatv("{0}", V.D).str()) + ")");
    if (V.Sym) {
      getStreamer().emitValue(wordsOf(V), 2, Loc);
    } else {
      if (V.I < -32768 || V.I > 65535)
        return Error(Loc, "Value " + Twine(V.I) +
                              " is not an 16-bit signed/unsigned integer");
      getStreamer().emitIntValue(V.I & 0xffff, 2);
    }
    if (Parser.getTok().isNot(AsmToken::Comma))
      break;
    Parser.Lex();
  }
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseDwordDirective() {
  while (true) {
    SMLoc Loc = Parser.getTok().getLoc();
    int64_t Val;
    if (parseEmasInt(Val, ".dword"))
      return true;
    if (Val < INT32_MIN || Val > UINT32_MAX)
      return Error(Loc, "Value won't fit in a DWORD: " + Twine(Val));
    getStreamer().emitIntValue(Val & 0xffffffff, 4);
    if (Parser.getTok().isNot(AsmToken::Comma))
      break;
    Parser.Lex();
  }
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseFloatDirective() {
  while (true) {
    SMLoc Loc = Parser.getTok().getLoc();
    EmasValue V;
    if (parseEmasExpr(V))
      return true;
    uint16_t Words[3];
    // The EMAWP library truncates; keep that behaviour for assembly sources.
    Mera400::FloatConvStatus Status = Mera400::convertDoubleToNativeFloat(
        V.asDouble(), /*RoundToNearest=*/false, Words);
    if (Status == Mera400::FloatConvStatus::Overflow ||
        Status == Mera400::FloatConvStatus::NotANumber)
      return Error(Loc, "Floating point overflow");
    if (Status == Mera400::FloatConvStatus::Underflow)
      return Error(Loc, "Floating point underflow");
    for (uint16_t W : Words)
      getStreamer().emitIntValue(W, 2);
    if (Parser.getTok().isNot(AsmToken::Comma))
      break;
    Parser.Lex();
  }
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseStringDirective(bool Terminate) {
  while (true) {
    SMLoc Loc = Parser.getTok().getLoc();
    if (Parser.getTok().isNot(AsmToken::String) ||
        !Parser.getTok().getString().starts_with("\""))
      return Error(Loc, "expected string");
    StringRef Body = Parser.getTok().getString().drop_front().drop_back();
    Parser.Lex();
    SmallVector<uint8_t, 32> Bytes;
    size_t Pos = 0;
    std::string Err;
    while (Pos < Body.size()) {
      unsigned C;
      if (!decodeEmasChar(Body, Pos, C, Err))
        return Error(Loc, Err);
      Bytes.push_back(C);
    }
    if (Terminate)
      Bytes.push_back(0);
    if (Bytes.size() % 2)
      Bytes.push_back(0);
    if (Bytes.size() / 2 > 65536)
      return Error(Loc, "Cannot fit the string in a process address space (" +
                            Twine(Bytes.size() / 2) + " words needed)");
    for (uint8_t B : Bytes)
      getStreamer().emitIntValue(B, 1);
    if (Parser.getTok().isNot(AsmToken::Comma))
      break;
    Parser.Lex();
  }
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseResDirective() {
  SMLoc Loc = Parser.getTok().getLoc();
  EmasValue Count;
  int64_t Fill = 0;
  if (parseEmasExpr(Count))
    return true;
  if (Count.IsFloat)
    return Error(Loc, ".res count requires an integer expression, got float (" +
                          Twine(formatv("{0}", Count.D).str()) + ")");
  if (Count.Reg)
    return Error(Loc, ".res count requires a constant expression");
  if (Parser.getTok().is(AsmToken::Comma)) {
    Parser.Lex();
    if (parseEmasInt(Fill, ".res fill value"))
      return true;
  }
  if (Count.Sym) {
    // A count defined later in the file is resolved when the object is laid
    // out. Count.Sym is in bytes (the count is a constant, so it is exact).
    if (InStruct)
      return Error(Loc, ".res count requires a constant expression");
    getStreamer().emitFill(
        *MCBinaryExpr::createDiv(Count.Sym, MCConstantExpr::create(2, getContext()),
                                 getContext()),
        2, Fill & 0xffff, Loc);
    return parseEndOfDirective();
  }
  if (Count.I < 0 || Count.I > 65536)
    return Error(Loc, "Cannot reserve memory outside the process address "
                      "space (requested " + Twine(Count.I) + " words)");
  if (InStruct) {
    StructOffset += Count.I;
    return parseEndOfDirective();
  }
  for (int64_t I = 0; I < Count.I; ++I)
    getStreamer().emitIntValue(Fill & 0xffff, 2);
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseOrgDirective() {
  SMLoc Loc = Parser.getTok().getLoc();
  EmasValue V;
  if (parseEmasExpr(V))
    return true;
  if (V.Sym)
    return Error(Loc, ".org argument must be a constant expression (no "
                      "forward references)");
  int64_t Words = V.I;
  if (V.IsFloat)
    return Error(Loc, ".org requires an integer expression, got float (" +
                          Twine(formatv("{0}", V.D).str()) + ")");
  if (V.Reg)
    return Error(Loc, ".org requires a constant expression");
  if (Words < 0 || Words > 65536)
    return Error(Loc, ".org address is outside the process address space");
  getStreamer().emitValueToOffset(
      MCConstantExpr::create(Words * 2, getContext()), 0, Loc);
  return parseEndOfDirective();
}

// Resolves a name in an expression: register, current location, local label,
// named constant, or otherwise a (possibly later defined) label.
bool Mera400AsmParser::resolveIdentifier(StringRef Name, SMLoc Loc,
                                         EmasValue &Val) {
  if (MCRegister Reg = MatchRegisterName(Name.lower())) {
    Val.Reg = Reg;
    return false;
  }
  if (Name == ".") {
    if (InStruct) {
      Val.I = StructOffset;
      return false;
    }
    MCSymbol *Here = getContext().createTempSymbol();
    getStreamer().emitLabel(Here, Loc);
    Val.Sym = MCSymbolRefExpr::create(Here, getContext());
    return false;
  }
  std::string Mangled;
  if (Name.starts_with(".")) {
    if (GlobalLabel.empty())
      return Error(Loc, "Cannot use local label \"" + Name +
                            "\" outside a global label context");
    Mangled = GlobalLabel + Name.str();
    Name = Mangled;
  }
  if (auto It = Constants.find(Name); It != Constants.end()) {
    bool Reg = Val.Reg.isValid();
    (void)Reg;
    Val = It->second;
    return false;
  }
  Val.Sym = MCSymbolRefExpr::create(getContext().getOrCreateSymbol(Name),
                                    getContext());
  return false;
}

bool Mera400AsmParser::defineConstant(StringRef Name, const EmasValue &Val,
                                      bool Redefinable, SMLoc Loc) {
  if (Val.Reg)
    return Error(Loc, "a constant cannot be a register");
  if (FixedConstants.contains(Name) || (!Redefinable && Constants.count(Name)))
    return Error(Loc, "Symbol '" + Name + "' already defined");
  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
  if (Sym->isDefined() && !Sym->isVariable())
    return Error(Loc, "Symbol '" + Name + "' already defined");
  Constants[Name] = Val;
  if (!Redefinable)
    FixedConstants.insert(Name);
  // The MC symbol makes the constant visible to forward references and to
  // .ifdef; its value is in bytes like every other symbol.
  const MCExpr *Bytes = Val.Sym ? Val.Sym
                        : Val.IsFloat
                            ? MCConstantExpr::create(0, getContext())
                            : MCConstantExpr::create(Val.I * 2, getContext());
  getStreamer().emitAssignment(Sym, Bytes);
  return false;
}

bool Mera400AsmParser::parseEquDirective(bool Redefinable) {
  SMLoc Loc = Parser.getTok().getLoc();
  StringRef Name;
  if (Parser.parseIdentifier(Name))
    return Error(Loc, "expected symbol name");
  if (Parser.getTok().is(AsmToken::Comma))
    Parser.Lex();
  EmasValue V;
  if (parseEmasExpr(V))
    return true;
  if (defineConstant(Name, V, Redefinable, Loc))
    return true;
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseStructDirective() {
  SMLoc Loc = Parser.getTok().getLoc();
  if (InStruct)
    return Error(Loc, "structs cannot be nested");
  StringRef Name;
  if (Parser.parseIdentifier(Name) || Parser.getTok().isNot(AsmToken::Colon))
    return Error(Parser.getTok().getLoc(),
                 "syntax error, unexpected symbol, expecting label");
  Parser.Lex();
  InStruct = true;
  StructName = Name.str();
  SavedGlobalLabel = GlobalLabel;
  GlobalLabel = StructName;
  StructOffset = 0;
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseEndStructDirective() {
  SMLoc Loc = Parser.getTok().getLoc();
  if (!InStruct)
    return Error(Loc, ".endstruct without .struct");
  InStruct = false;
  GlobalLabel = SavedGlobalLabel;
  EmasValue Size;
  Size.I = StructOffset;
  if (defineConstant(StructName, Size, false, Loc))
    return true;
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseCpuDirective() {
  SMLoc Loc = Parser.getTok().getLoc();
  StringRef Name;
  if (Parser.parseIdentifier(Name))
    return Error(Loc, "expected CPU name");
  if (CpuSet)
    return Error(Loc, "CPU type already set.");
  if (Name.equals_insensitive("mx16"))
    Mx16 = true;
  else if (!Name.equals_insensitive("mera400"))
    return Error(Loc, "Unknown CPU type '" + Name + "'.");
  CpuSet = true;
  return parseEndOfDirective();
}

bool Mera400AsmParser::parseModelDirective() {
  SMLoc Loc = Parser.getTok().getLoc();
  StringRef Name;
  if (Parser.parseIdentifier(Name))
    return Error(Loc, "expected memory model (small or large)");
  bool Large;
  if (Name.equals_insensitive("large"))
    Large = true;
  else if (Name.equals_insensitive("small"))
    Large = false;
  else
    return Error(Loc, "Unknown memory model '" + Name +
                          "' (expected small or large)");
  if (ModelSet && ModelLarge != Large)
    return Error(Loc, "memory model already set to " +
                          Twine(ModelLarge ? "large" : "small"));
  if (!ModelSet)
    if (auto *TS = static_cast<Mera400TargetStreamer *>(
            Parser.getStreamer().getTargetStreamer()))
      TS->emitModel(Large);
  ModelSet = true;
  ModelLarge = Large;
  return parseEndOfDirective();
}

// "name:" inside a .struct: the field is a constant holding its offset.
bool Mera400AsmParser::parseStructField(StringRef Name, SMLoc Loc) {
  Parser.Lex(); // ':'
  EmasValue Offset;
  Offset.I = StructOffset;
  return defineConstant(Name, Offset, false, Loc);
}

// ".name:" defines a label scoped to the preceding global label (or a struct
// field named "struct.name" inside a .struct).
bool Mera400AsmParser::parseLocalLabel(StringRef Name, SMLoc Loc) {
  if (GlobalLabel.empty())
    return Error(Loc, "Cannot define local label \"" + Name +
                          "\" outside a global label context");
  Parser.Lex(); // ':'
  std::string Full = GlobalLabel + Name.str();
  if (InStruct) {
    EmasValue Offset;
    Offset.I = StructOffset;
    return defineConstant(Full, Offset, false, Loc);
  }
  if (Parser.checkForValidSection())
    return true;
  getStreamer().emitLabel(getContext().getOrCreateSymbol(Full), Loc);
  return false;
}

// Local labels (".name") are also valid in generic directives such as .size,
// which compilers emit as ".size f, .Lfunc_end0-f".
bool Mera400AsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  const AsmToken &Tok = Parser.getTok();
  if (Tok.is(AsmToken::Identifier) && Tok.getString().size() > 1 &&
      Tok.getString().starts_with(".")) {
    if (GlobalLabel.empty())
      return Error(Tok.getLoc(), "Cannot use local label \"" +
                                     Tok.getString() +
                                     "\" outside a global label context");
    MCSymbol *Sym = getContext().getOrCreateSymbol(GlobalLabel +
                                                   Tok.getString().str());
    Res = MCSymbolRefExpr::create(Sym, getContext());
    EndLoc = Tok.getEndLoc();
    Parser.Lex();
    return false;
  }
  // words(expr), as printed by the compiler for pointers in data: the
  // byte-valued expression halved into a word address.
  if (Tok.is(AsmToken::Identifier) && Tok.getString() == "words" &&
      getLexer().peekTok().is(AsmToken::LParen)) {
    Parser.Lex();
    Parser.Lex();
    const MCExpr *Inner;
    if (Parser.parseExpression(Inner))
      return true;
    if (Parser.getTok().isNot(AsmToken::RParen))
      return Error(Parser.getTok().getLoc(), "expected ')'");
    EndLoc = Parser.getTok().getEndLoc();
    Parser.Lex();
    Res = Mera400HalfExpr::create(Inner, getContext());
    return false;
  }
  return MCTargetAsmParser::parsePrimaryExpr(Res, EndLoc);
}

ParseStatus Mera400AsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef Name = DirectiveID.getIdentifier();
  if (Name.size() > 1 && Parser.getTok().is(AsmToken::Colon))
    return ParseStatus(parseLocalLabel(Name, DirectiveID.getLoc()));
  if (Name == ".equ")
    return ParseStatus(parseEquDirective(true));
  if (Name == ".const")
    return ParseStatus(parseEquDirective(false));
  if (Name == ".struct")
    return ParseStatus(parseStructDirective());
  if (Name == ".endstruct")
    return ParseStatus(parseEndStructDirective());
  if (Name == ".cpu")
    return ParseStatus(parseCpuDirective());
  if (Name == ".model")
    return ParseStatus(parseModelDirective());
  bool (Mera400AsmParser::*Handler)() = nullptr;
  bool Terminate = false;
  if (Name == ".word")
    Handler = &Mera400AsmParser::parseWordDirective;
  else if (Name == ".dword")
    Handler = &Mera400AsmParser::parseDwordDirective;
  else if (Name == ".float")
    Handler = &Mera400AsmParser::parseFloatDirective;
  else if (Name == ".res")
    Handler = &Mera400AsmParser::parseResDirective;
  else if (Name == ".org")
    Handler = &Mera400AsmParser::parseOrgDirective;
  else if (Name == ".ascii" || Name == ".asciiz")
    Terminate = Name == ".asciiz";
  else
    return ParseStatus::NoMatch;
  Parser.getStreamer().getCurrentSectionOnly()->ensureMinAlignment(Align(2));
  return ParseStatus(Handler ? (this->*Handler)()
                             : parseStringDirective(Terminate));
}

// Operand expressions are EMAS expressions; they may be symbolic (a sum or
// difference of symbols and constants) but never floating point.
bool Mera400AsmParser::parseOperandExpr(const MCExpr *&Expr,
                                        MCRegister *Index) {
  SMLoc Loc = Parser.getTok().getLoc();
  EmasValue V;
  if (parseEmasExpr(V))
    return true;
  if (V.IsFloat)
    return Error(Loc, "instruction operand or .word requires an integer "
                      "expression, got float (" +
                          Twine(formatv("{0}", V.D).str()) + ")");
  if (V.Reg) {
    if (!Index)
      return Error(Loc, "unexpected register in expression");
    *Index = V.Reg;
  }
  Expr = wordsOf(V);
  return false;
}

// Parses the inside of a normal argument: rC, rC+rB, val, rB+val, val+rB or
// rB-val. A register is told apart from a symbol before parsing an expression,
// so "val+rB" is recognized by splitting a top-level addition afterwards.
bool Mera400AsmParser::parseNormValue(OperandVector &Operands, bool Indirect,
                                      SMLoc Start) {
  MCRegister Reg;
  SMLoc RegStart, RegEnd;
  const MCExpr *Expr;
  if (tryParseRegister(Reg, RegStart, RegEnd).isSuccess()) {
    MCRegister Second;
    if (Parser.getTok().is(AsmToken::Plus)) {
      Parser.Lex();
      if (tryParseRegister(Second, RegStart, RegEnd).isSuccess()) {
        Operands.push_back(Mera400Operand::createNorm(
            Indirect, Reg, Second, nullptr, Start, RegEnd));
        return false;
      }
      if (parseOperandExpr(Expr))
        return true;
      Operands.push_back(Mera400Operand::createNorm(
          Indirect, MCRegister(), Reg, Expr, Start, Parser.getTok().getLoc()));
      return false;
    }
    if (Parser.getTok().is(AsmToken::Minus)) {
      Parser.Lex();
      if (parseOperandExpr(Expr))
        return true;
      Expr = MCUnaryExpr::createMinus(Expr, getContext());
      Operands.push_back(Mera400Operand::createNorm(
          Indirect, MCRegister(), Reg, Expr, Start, Parser.getTok().getLoc()));
      return false;
    }
    Operands.push_back(Mera400Operand::createNorm(Indirect, Reg, MCRegister(),
                                                  nullptr, Start, RegEnd));
    return false;
  }

  MCRegister Index;
  if (parseOperandExpr(Expr, &Index))
    return true;
  SMLoc End = Parser.getTok().getLoc();
  if (Indirect || Index)
    Operands.push_back(Mera400Operand::createNorm(Indirect, MCRegister(),
                                                  Index, Expr, Start, End));
  else
    Operands.push_back(std::make_unique<Mera400Operand>(Expr, Start, End));
  return false;
}

bool Mera400AsmParser::parseNormArg(OperandVector &Operands, bool Indirect) {
  SMLoc Start = Parser.getTok().getLoc();
  if (Indirect) {
    Parser.Lex();
    if (parseNormValue(Operands, true, Start))
      return true;
    if (Parser.getTok().isNot(AsmToken::RBrac))
      return Error(Parser.getTok().getLoc(), "expected ']'");
    Parser.Lex();
    return false;
  }
  return parseNormValue(Operands, false, Start);
}

bool Mera400AsmParser::parseInstruction(ParseInstructionInfo &, StringRef Name,
                                        SMLoc NameLoc,
                                        OperandVector &Operands) {
  Operands.push_back(std::make_unique<Mera400Operand>(Name, NameLoc));
  if (InStruct && Parser.getTok().is(AsmToken::Colon)) {
    // The matcher is skipped for this pseudo-statement.
    StructFieldParsed = true;
    return parseStructField(Name, NameLoc);
  }
  if (Parser.getTok().is(AsmToken::EndOfStatement)) {
    Parser.Lex();
    return false;
  }
  // EMAS allows several operand-less instructions on one line ("nop nop").
  static const StringSet<> NoOperandOps = {"nop", "mcl", "cit", "sil", "siu",
                                           "sit", "giu", "lip", "gil", "cron",
                                           "sint", "sind"};
  if (NoOperandOps.contains(Name.lower()) &&
      Parser.getTok().is(AsmToken::Identifier))
    return false;

  while (true) {
    MCRegister Reg;
    SMLoc Start, End;
    if (Parser.getTok().is(AsmToken::LBrac)) {
      if (parseNormArg(Operands, true))
        return true;
    } else if (Parser.getTok().is(AsmToken::Identifier) &&
               MatchRegisterName(Parser.getTok().getIdentifier().lower())) {
      // Might be rA or the start of a normal argument such as "r1+r2".
      AsmToken Next = Parser.getLexer().peekTok();
      if (Next.is(AsmToken::Plus) || Next.is(AsmToken::Minus)) {
        if (parseNormArg(Operands, false))
          return true;
      } else {
        tryParseRegister(Reg, Start, End);
        Operands.push_back(std::make_unique<Mera400Operand>(Reg, Start, End));
      }
    } else {
      if (parseNormArg(Operands, false))
        return true;
    }

    if (Parser.getTok().isNot(AsmToken::Comma))
      break;
    Parser.Lex();
  }

  if (Parser.getTok().isNot(AsmToken::EndOfStatement))
    return Error(Parser.getTok().getLoc(), "unexpected token in instruction");
  Parser.Lex();
  return false;
}

bool Mera400AsmParser::matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  if (StructFieldParsed) {
    StructFieldParsed = false;
    return false;
  }
  MCInst Inst;
  unsigned Result =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);
  if (Result == Match_Success) {
    unsigned Op = Inst.getOpcode();
    if (!Mx16 && (Op == Mera400::CRON || Op == Mera400::SINT ||
                  Op == Mera400::SIND))
      return Error(Loc, "Instruction valid only for MX-16");
    Inst.setLoc(Loc);
    Out.getCurrentSectionOnly()->ensureMinAlignment(Align(2));
    Out.emitInstruction(Inst, getSTI());
    return false;
  }
  if (Result == Match_MnemonicFail)
    return Error(Loc, "invalid or unsupported instruction mnemonic");
  if (Result == Match_InvalidOperand) {
    SMLoc OperandLoc =
        ErrorInfo < Operands.size() ? Operands[ErrorInfo]->getStartLoc() : Loc;
    return Error(OperandLoc, "invalid operand for MERA-400 instruction");
  }
  return Error(Loc, "unsupported MERA-400 instruction");
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMera400AsmParser() {
  RegisterMCAsmParser<Mera400AsmParser> X(getTheMera400Target());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "Mera400GenAsmMatcher.inc"
