// Nobify semantic clang-tidy checks.

#include "clang-tidy/ClangTidyCheck.h"
#include "clang-tidy/ClangTidyModule.h"
#include "clang-tidy/ClangTidyModuleRegistry.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <stdint.h>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;

namespace clang::tidy::nobify {

static llvm::StringRef fileName(const SourceManager &SM, SourceLocation Loc) {
    if (Loc.isInvalid()) return {};
    return SM.getFilename(SM.getSpellingLoc(Loc));
}

static bool contains(llvm::StringRef Haystack, llvm::StringRef Needle) {
    return Haystack.find(Needle) != llvm::StringRef::npos;
}

static std::string semanticNameRegex(const char *Regex) {
    if (!Regex) return {};
    llvm::StringRef Ref(Regex);
    if (Ref.starts_with("^")) return ("(^|::)" + Ref.drop_front()).str();
    return Ref.str();
}

static bool isFixturePath(llvm::StringRef Path) {
    return contains(Path, "test_v2/semantic_tidy/fixtures/");
}

static bool isEvaluatorPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/evaluator/") || isFixturePath(Path);
}

static bool isBuildModelOwnerPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/build_model/");
}

static bool isProductionBuildModelConsumerPath(llvm::StringRef Path) {
    if (isFixturePath(Path)) return true;
    return contains(Path, "src_v2/codegen/") || contains(Path, "src_v2/app/") ||
           (contains(Path, "src_v2/") && !isBuildModelOwnerPath(Path) &&
            !contains(Path, "test_v2/"));
}

static bool isCodegenBoundaryPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/codegen/") || isFixturePath(Path);
}

static bool isParserOrLexerPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/parser/") || contains(Path, "src_v2/lexer/");
}

static bool isEventIrOwnerPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/transpiler/");
}

static bool isEvaluatorOwnerFile(llvm::StringRef Path) {
    return Path.ends_with("src_v2/evaluator/evaluator.c");
}

static bool isEvaluatorFileHandlerPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/evaluator/eval_file") || isFixturePath(Path);
}

static bool isBuildModelConstructionPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/build_model/build_model_builder") ||
           contains(Path, "src_v2/build_model/build_model_freeze.c");
}

static bool isBuildModelQueryPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/build_model/build_model_query");
}

static bool isBuildModelValidatePath(llvm::StringRef Path) {
    return contains(Path, "src_v2/build_model/build_model_validate");
}

static bool isPipelineOrchestrationPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/app/") ||
           contains(Path, "test_v2/test_semantic_pipeline") ||
           contains(Path, "test_v2/codegen/") ||
           contains(Path, "test_v2/evaluator_codegen_diff/") ||
           contains(Path, "test_v2/artifact_parity/");
}

static bool isEvaluatorHostServiceConsumerPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/evaluator/eval_try_compile") ||
           contains(Path, "src_v2/evaluator/eval_flow_process.c") ||
           contains(Path, "src_v2/evaluator/eval_host.c") ||
           contains(Path, "src_v2/evaluator/eval_expr.c") ||
           contains(Path, "src_v2/evaluator/eval_vars.c") ||
           contains(Path, "src_v2/evaluator/eval_package_find_item.c") ||
           contains(Path, "src_v2/evaluator/eval_ctest.c") ||
           contains(Path, "src_v2/evaluator/eval_meta.c") ||
           contains(Path, "src_v2/evaluator/eval_utils_path.c") ||
           contains(Path, "src_v2/evaluator/evaluator.c");
}

static bool isPureSemanticLayerPath(llvm::StringRef Path) {
    return contains(Path, "src_v2/parser/") ||
           contains(Path, "src_v2/lexer/") ||
           contains(Path, "src_v2/transpiler/") ||
           contains(Path, "src_v2/build_model/") ||
           contains(Path, "src_v2/codegen/");
}

static bool typeNamesEvalResult(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    return Name == "Eval_Result" || Name == "struct Eval_Result" ||
           llvm::StringRef(Name).ends_with(" Eval_Result");
}

static bool typeNamesEventIr(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    if (Ref.starts_with("Event_") || Ref.starts_with("struct Event_") ||
        Ref.starts_with("enum Event_")) {
        return true;
    }
    return Ref.contains(" Event_");
}

static bool typeNamesCodegen(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    return Ref.starts_with("Nob_Codegen_") || Ref.starts_with("struct Nob_Codegen_") ||
           Ref.starts_with("CG_") || Ref.starts_with("struct CG_") ||
           Ref.contains(" Nob_Codegen_") || Ref.contains(" CG_");
}

static bool typeNamesFrozenBuildModel(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    if (Ref.contains("Build_Model_Draft")) return false;
    return Ref == "Build_Model" || Ref == "struct Build_Model" ||
           Ref.contains(" Build_Model");
}

static bool typeNamesBuildModelLifecycle(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    return Ref == "BM_Builder" || Ref == "struct BM_Builder" ||
           Ref == "Build_Model_Builder" || Ref == "struct Build_Model_Builder" ||
           Ref == "Build_Model_Draft" || Ref == "struct Build_Model_Draft" ||
           Ref.contains(" BM_Builder") || Ref.contains(" Build_Model_Builder") ||
           Ref.contains(" Build_Model_Draft");
}

static bool typeNamesBuildModelBuilderState(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    return Ref == "BM_Builder" || Ref == "struct BM_Builder" ||
           Ref == "Build_Model_Builder" || Ref == "struct Build_Model_Builder" ||
           Ref.contains(" BM_Builder") || Ref.contains(" Build_Model_Builder");
}

static bool typeNamesBuildModelFrozenRecord(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    return typeNamesFrozenBuildModel(Type) || Ref.contains("_Record");
}

static bool typeNamesEvaluatorState(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    return Ref == "EvalExecContext" || Ref == "struct EvalExecContext" ||
           Ref == "Eval_Result" || Ref == "struct Eval_Result" ||
           Ref.contains(" EvalExecContext") || Ref.contains(" Eval_Result");
}

static bool typeNamesParserState(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    llvm::StringRef Ref(Name);
    return Ref.starts_with("Parser") || Ref.starts_with("struct Parser") ||
           Ref.starts_with("Ast_") || Ref.starts_with("struct Ast_") ||
           Ref.contains(" Parser") || Ref.contains(" Ast_");
}

static bool typeNamesFrozenModelOrBuilderState(QualType Type) {
    return typeNamesFrozenBuildModel(Type) || typeNamesBuildModelBuilderState(Type);
}

static bool typeNamesBuildModelAnyState(QualType Type) {
    return typeNamesFrozenBuildModel(Type) || typeNamesBuildModelLifecycle(Type);
}

static bool typeNamesEvaluatorOrParserState(QualType Type) {
    return typeNamesEvaluatorState(Type) || typeNamesParserState(Type);
}

static bool typeNamesParserDownstreamState(QualType Type) {
    return typeNamesEventIr(Type) || typeNamesEvaluatorState(Type) ||
           typeNamesBuildModelAnyState(Type) || typeNamesCodegen(Type);
}

static bool typeNamesEventIrDownstreamState(QualType Type) {
    return typeNamesBuildModelAnyState(Type) || typeNamesCodegen(Type);
}

typedef bool (*SemanticLayerAppliesFn)(llvm::StringRef Path, const FunctionDecl *FD);
typedef bool (*SemanticTypeMatcherFn)(QualType Type);

typedef struct {
    SemanticLayerAppliesFn applies;
    SemanticTypeMatcherFn forbidden_type;
    const char *var_message;
    const char *param_message;
    const char *return_message;
    const char *call_message;
} Semantic_Layer_Dependency_Rule;

static void registerLayerDependencyMatchers(MatchFinder *Finder,
                                            ClangTidyCheck *Check,
                                            const char *ForbiddenCallRegex) {
    Finder->addMatcher(varDecl(hasAncestor(functionDecl().bind("function"))).bind("layer-var"),
                       Check);
    Finder->addMatcher(parmVarDecl(hasAncestor(functionDecl().bind("function"))).bind("layer-param"),
                       Check);
    Finder->addMatcher(functionDecl(isDefinition()).bind("layer-return-function"), Check);
    if (ForbiddenCallRegex && ForbiddenCallRegex[0] != '\0') {
        std::string CalleeRegex = semanticNameRegex(ForbiddenCallRegex);
        Finder->addMatcher(callExpr(callee(functionDecl(matchesName(CalleeRegex))),
                                    hasAncestor(functionDecl().bind("layer-call-function")))
                               .bind("layer-call"),
                           Check);
    }
}

static void checkLayerDependency(const MatchFinder::MatchResult &Result,
                                 ClangTidyCheck &Check,
                                 const Semantic_Layer_Dependency_Rule &Rule) {
    const SourceManager &SM = *Result.SourceManager;

    if (const auto *VD = Result.Nodes.getNodeAs<VarDecl>("layer-var")) {
        const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("function");
        llvm::StringRef Path = fileName(SM, VD->getLocation());
        if (!Rule.applies(Path, FD) || isa<ParmVarDecl>(VD)) return;
        if (Rule.forbidden_type && Rule.forbidden_type(VD->getType())) {
            Check.diag(VD->getLocation(), Rule.var_message);
        }
        return;
    }

    if (const auto *PD = Result.Nodes.getNodeAs<ParmVarDecl>("layer-param")) {
        const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("function");
        llvm::StringRef Path = fileName(SM, PD->getLocation());
        if (!Rule.applies(Path, FD)) return;
        if (Rule.forbidden_type && Rule.forbidden_type(PD->getType())) {
            Check.diag(PD->getLocation(), Rule.param_message);
        }
        return;
    }

    if (const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("layer-return-function")) {
        llvm::StringRef Path = fileName(SM, FD->getLocation());
        if (!Rule.applies(Path, FD)) return;
        if (Rule.forbidden_type && Rule.forbidden_type(FD->getReturnType())) {
            Check.diag(FD->getLocation(), Rule.return_message);
        }
        return;
    }

    if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("layer-call")) {
        const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("layer-call-function");
        llvm::StringRef Path = fileName(SM, Call->getExprLoc());
        if (!Rule.applies(Path, FD)) return;
        Check.diag(Call->getExprLoc(), Rule.call_message);
    }
}

typedef struct {
    SemanticLayerAppliesFn applies;
    const char *message;
} Semantic_Forbidden_Call_Rule;

static void registerForbiddenCallMatcher(MatchFinder *Finder,
                                         ClangTidyCheck *Check,
                                         const char *ForbiddenCallRegex) {
    std::string CalleeRegex = semanticNameRegex(ForbiddenCallRegex);
    Finder->addMatcher(callExpr(callee(functionDecl(matchesName(CalleeRegex))),
                                hasAncestor(functionDecl().bind("forbidden-call-function")))
                           .bind("forbidden-call"),
                       Check);
}

static void checkForbiddenCall(const MatchFinder::MatchResult &Result,
                               ClangTidyCheck &Check,
                               const Semantic_Forbidden_Call_Rule &Rule) {
    const auto *Call = Result.Nodes.getNodeAs<CallExpr>("forbidden-call");
    const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("forbidden-call-function");
    if (!Call || !FD) return;

    llvm::StringRef Path = fileName(*Result.SourceManager, Call->getExprLoc());
    if (!Rule.applies(Path, FD)) return;

    Check.diag(Call->getExprLoc(), Rule.message);
}

typedef struct {
    SemanticLayerAppliesFn applies;
    const char *message;
} Semantic_Global_State_Rule;

static void registerGlobalStateMatcher(MatchFinder *Finder, ClangTidyCheck *Check) {
    Finder->addMatcher(varDecl().bind("global-state"), Check);
}

static void checkGlobalState(const MatchFinder::MatchResult &Result,
                             ClangTidyCheck &Check,
                             const Semantic_Global_State_Rule &Rule) {
    const auto *VD = Result.Nodes.getNodeAs<VarDecl>("global-state");
    if (!VD) return;
    if (!VD->isFileVarDecl() && !VD->isStaticLocal()) return;
    if (VD->getType().isConstQualified()) return;

    llvm::StringRef Path = fileName(*Result.SourceManager, VD->getLocation());
    if (!Rule.applies(Path, nullptr)) return;

    Check.diag(VD->getLocation(), Rule.message);
}

typedef bool (*SemanticMemberAppliesFn)(llvm::StringRef Path,
                                        ASTContext *Context,
                                        const MemberExpr *Member);
typedef bool (*SemanticRecordMatcherFn)(const RecordDecl *Record);

typedef struct {
    SemanticMemberAppliesFn applies;
    SemanticRecordMatcherFn record_matches;
    const char *message;
} Semantic_Member_Access_Rule;

static bool ancestorFunctionNameContains(ASTContext *Context,
                                         const MemberExpr *Member,
                                         llvm::StringRef Needle) {
    if (!Context || !Member) return false;

    DynTypedNodeList Parents = Context->getParents(*Member);
    while (!Parents.empty()) {
        if (const auto *FD = Parents[0].get<FunctionDecl>()) {
            return FD->getName().contains(Needle);
        }
        if (const auto *StmtParent = Parents[0].get<Stmt>()) {
            Parents = Context->getParents(*StmtParent);
            continue;
        }
        if (const auto *DeclParent = Parents[0].get<Decl>()) {
            Parents = Context->getParents(*DeclParent);
            continue;
        }
        break;
    }
    return false;
}

static const RecordDecl *recordFromMemberExpr(const MemberExpr *Member) {
    if (!Member) return nullptr;
    const FieldDecl *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
    return Field ? Field->getParent() : nullptr;
}

static void registerMemberAccessMatcher(MatchFinder *Finder, ClangTidyCheck *Check) {
    Finder->addMatcher(memberExpr().bind("member-access"), Check);
}

static void registerMemberMutationMatcher(MatchFinder *Finder, ClangTidyCheck *Check) {
    auto Member = memberExpr().bind("member-mutation");
    Finder->addMatcher(binaryOperator(isAssignmentOperator(),
                                      hasLHS(ignoringParenImpCasts(Member))),
                       Check);
    Finder->addMatcher(unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--")),
                                     hasUnaryOperand(ignoringParenImpCasts(Member))),
                       Check);
}

static void registerNamedFieldMutationMatcher(MatchFinder *Finder,
                                              ClangTidyCheck *Check,
                                              const char *FieldRegex) {
    auto Member = memberExpr(member(fieldDecl(matchesName(FieldRegex)))).bind("member-mutation");
    Finder->addMatcher(binaryOperator(isAssignmentOperator(),
                                      hasLHS(ignoringParenImpCasts(Member))),
                       Check);
    Finder->addMatcher(unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--")),
                                     hasUnaryOperand(ignoringParenImpCasts(Member))),
                       Check);
}

static void checkMemberAccess(const MatchFinder::MatchResult &Result,
                              ClangTidyCheck &Check,
                              const Semantic_Member_Access_Rule &Rule,
                              const char *BindName) {
    const auto *Member = Result.Nodes.getNodeAs<MemberExpr>(BindName);
    if (!Member) return;

    llvm::StringRef Path = fileName(*Result.SourceManager, Member->getExprLoc());
    if (!Rule.applies(Path, Result.Context, Member)) return;

    const RecordDecl *Record = recordFromMemberExpr(Member);
    if (Record && Rule.record_matches(Record)) {
        Check.diag(Member->getExprLoc(), Rule.message);
    }
}

static bool recordIsBuildModelOrBMRecord(const RecordDecl *Record) {
    if (!Record) return false;
    llvm::StringRef Name = Record->getName();
    return Name == "Build_Model" || Name.starts_with("BM_");
}

static bool recordIsFrozenBuildModelRecord(const RecordDecl *Record) {
    if (!Record) return false;
    llvm::StringRef Name = Record->getName();
    return Name == "Build_Model" || Name.ends_with("_Record");
}

static bool recordIsDraftOrFrozenBuildModelRecord(const RecordDecl *Record) {
    if (!Record) return false;
    llvm::StringRef Name = Record->getName();
    return Name == "Build_Model_Draft" || Name == "Build_Model" || Name.ends_with("_Record");
}

static bool recordIsEvalExecContext(const RecordDecl *Record) {
    return Record && Record->getName() == "EvalExecContext";
}

typedef bool (*SemanticStringLiteralMatcherFn)(llvm::StringRef Value);

typedef struct {
    SemanticLayerAppliesFn applies;
    SemanticStringLiteralMatcherFn literal_matches;
    const char *message;
} Semantic_Call_Literal_Rule;

static void registerCallLiteralMatcher(MatchFinder *Finder,
                                       ClangTidyCheck *Check,
                                       const char *CalleeName) {
    Finder->addMatcher(callExpr(callee(functionDecl(hasName(CalleeName))),
                                hasDescendant(stringLiteral().bind("semantic-literal")),
                                hasAncestor(functionDecl().bind("semantic-literal-function")))
                           .bind("semantic-literal-call"),
                       Check);
}

static void checkCallLiteral(const MatchFinder::MatchResult &Result,
                             ClangTidyCheck &Check,
                             const Semantic_Call_Literal_Rule &Rule) {
    const auto *Call = Result.Nodes.getNodeAs<CallExpr>("semantic-literal-call");
    const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("semantic-literal-function");
    const auto *Literal = Result.Nodes.getNodeAs<StringLiteral>("semantic-literal");
    if (!Call || !FD || !Literal) return;

    llvm::StringRef Path = fileName(*Result.SourceManager, Call->getExprLoc());
    if (!Rule.applies(Path, FD)) return;
    if (!Rule.literal_matches(Literal->getString())) return;

    Check.diag(Call->getExprLoc(), Rule.message);
}

typedef bool (*SemanticCallPhaseMatcherFn)(const FunctionDecl *FD);

typedef struct {
    SemanticLayerAppliesFn applies;
    const SemanticCallPhaseMatcherFn *phase_matchers;
    size_t phase_count;
    const char *message;
} Semantic_Phase_Chain_Rule;

class SemanticPhaseVisitor : public RecursiveASTVisitor<SemanticPhaseVisitor> {
public:
    explicit SemanticPhaseVisitor(const Semantic_Phase_Chain_Rule &Rule) : Rule(Rule) {}

    bool VisitCallExpr(CallExpr *Call) {
        const FunctionDecl *FD = Call ? Call->getDirectCallee() : nullptr;
        if (!FD) return true;
        for (size_t I = 0; I < Rule.phase_count && I < 64; ++I) {
            if (Rule.phase_matchers[I] && Rule.phase_matchers[I](FD)) {
                SeenMask |= (uint64_t{1} << I);
            }
        }
        return true;
    }

    unsigned phaseCount() const {
        uint64_t Mask = SeenMask;
        unsigned Count = 0;
        while (Mask) {
            Count += (unsigned)(Mask & 1u);
            Mask >>= 1u;
        }
        return Count;
    }

private:
    const Semantic_Phase_Chain_Rule &Rule;
    uint64_t SeenMask = 0;
};

static void registerPhaseChainMatcher(MatchFinder *Finder, ClangTidyCheck *Check) {
    Finder->addMatcher(functionDecl(isDefinition()).bind("phase-chain-function"), Check);
}

static void checkPhaseChain(const MatchFinder::MatchResult &Result,
                            ClangTidyCheck &Check,
                            const Semantic_Phase_Chain_Rule &Rule) {
    const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("phase-chain-function");
    if (!FD || !FD->hasBody()) return;

    llvm::StringRef Path = fileName(*Result.SourceManager, FD->getLocation());
    if (!Rule.applies(Path, FD)) return;

    SemanticPhaseVisitor Visitor(Rule);
    Visitor.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
    if (Visitor.phaseCount() < 2) return;

    Check.diag(FD->getLocation(), Rule.message);
}

static bool functionReturnsEvalResult(const FunctionDecl *FD) {
    return FD && typeNamesEvalResult(FD->getReturnType());
}

static bool isEvalHandleFunction(const FunctionDecl *FD) {
    return FD && FD->getName().starts_with("eval_handle_");
}

static const Expr *stripExpr(const Expr *E) {
    if (!E) return nullptr;
    return E->IgnoreParenImpCasts();
}

static bool calleeNamed(const CallExpr *Call, llvm::StringRef Name) {
    const FunctionDecl *FD = Call ? Call->getDirectCallee() : nullptr;
    return FD && FD->getName() == Name;
}

static bool boolLiteralValue(const Expr *E, bool *Out) {
    E = stripExpr(E);
    if (!E) return false;
    if (const auto *BL = dyn_cast<CXXBoolLiteralExpr>(E)) {
        *Out = BL->getValue();
        return true;
    }
    if (const auto *IL = dyn_cast<IntegerLiteral>(E)) {
        if (IL->getValue() == 0) {
            *Out = false;
            return true;
        }
        if (IL->getValue() == 1) {
            *Out = true;
            return true;
        }
    }
    return false;
}

static std::string sourceText(ASTContext &Context, SourceRange Range) {
    const SourceManager &SM = Context.getSourceManager();
    const LangOptions &LangOpts = Context.getLangOpts();
    CharSourceRange CharRange = CharSourceRange::getTokenRange(Range);
    return Lexer::getSourceText(CharRange, SM, LangOpts).str();
}

enum class StopProjection {
    Unknown,
    Positive,
    Negative,
};

static StopProjection invert(StopProjection Value) {
    if (Value == StopProjection::Positive) return StopProjection::Negative;
    if (Value == StopProjection::Negative) return StopProjection::Positive;
    return StopProjection::Unknown;
}

class FunctionStopAnalyzer {
public:
    void reset() { Aliases.clear(); }

    StopProjection classify(const Expr *E) const {
        E = stripExpr(E);
        if (!E) return StopProjection::Unknown;
        if (const auto *Call = dyn_cast<CallExpr>(E)) {
            if (calleeNamed(Call, "eval_should_stop")) return StopProjection::Positive;
        }
        if (const auto *Unary = dyn_cast<UnaryOperator>(E)) {
            if (Unary->getOpcode() == UO_LNot) return invert(classify(Unary->getSubExpr()));
        }
        if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
            const auto *VD = dyn_cast<VarDecl>(Ref->getDecl());
            if (!VD) return StopProjection::Unknown;
            auto It = Aliases.find(VD);
            if (It == Aliases.end()) return StopProjection::Unknown;
            return It->second;
        }
        return StopProjection::Unknown;
    }

    void collectAlias(const VarDecl *VD) {
        if (!VD || !VD->hasInit()) return;
        StopProjection Projection = classify(VD->getInit());
        if (Projection != StopProjection::Unknown) Aliases[VD] = Projection;
    }

private:
    llvm::DenseMap<const VarDecl *, StopProjection> Aliases;
};

static bool isEvalResultAllowedConsumer(const CallExpr *Call) {
    const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
    if (!Callee) return false;
    llvm::StringRef Name = Callee->getName();
    return Name == "eval_result_is_ok" || Name == "eval_result_is_soft_error" ||
           Name == "eval_result_is_fatal" || Name == "eval_result_merge";
}

static bool exprReturnsEvalResult(const Expr *E) {
    E = stripExpr(E);
    if (!E) return false;
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
        return functionReturnsEvalResult(Call->getDirectCallee());
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
        if (const auto *VD = dyn_cast<VarDecl>(Ref->getDecl())) {
            return typeNamesEvalResult(VD->getType());
        }
    }
    return typeNamesEvalResult(E->getType());
}

static bool isEvalResultKindAccess(const Expr *E) {
    E = stripExpr(E);
    const auto *Member = dyn_cast_or_null<MemberExpr>(E);
    if (!Member) return false;
    const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
    if (!Field || Field->getName() != "kind") return false;
    const Expr *Base = stripExpr(Member->getBase());
    return Base && typeNamesEvalResult(Base->getType());
}

class EvalResultLocalUseVisitor : public RecursiveASTVisitor<EvalResultLocalUseVisitor> {
public:
    explicit EvalResultLocalUseVisitor(llvm::DenseMap<const VarDecl *, unsigned> &Uses)
        : Uses(Uses) {}

    bool VisitDeclRefExpr(DeclRefExpr *Ref) {
        if (const auto *VD = dyn_cast<VarDecl>(Ref->getDecl())) {
            auto It = Uses.find(VD);
            if (It != Uses.end()) It->second++;
        }
        return true;
    }

private:
    llvm::DenseMap<const VarDecl *, unsigned> &Uses;
};

class EvalResultPropagationCheck : public ClangTidyCheck {
public:
    EvalResultPropagationCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
        Finder->addMatcher(callExpr().bind("call"), this);
        Finder->addMatcher(varDecl(hasInitializer(expr())).bind("var"), this);
        Finder->addMatcher(binaryOperator(isAssignmentOperator()).bind("assign"), this);
        Finder->addMatcher(returnStmt(hasReturnValue(expr())).bind("return"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const SourceManager &SM = *Result.SourceManager;
        if (const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("function")) {
            llvm::StringRef Path = fileName(SM, FD->getLocation());
            if (!isEvaluatorPath(Path)) return;
            if (FD->getName().starts_with("eval_handle_") && !functionReturnsEvalResult(FD)) {
                diag(FD->getLocation(), "eval_handle_* functions must return Eval_Result");
            }
            checkUnusedEvalResultLocal(*Result.Context, FD);
            return;
        }

        if (const auto *VD = Result.Nodes.getNodeAs<VarDecl>("var")) {
            llvm::StringRef Path = fileName(SM, VD->getLocation());
            if (!isEvaluatorPath(Path)) return;
            if ((VD->getType()->isBooleanType() || VD->getType()->isIntegerType()) &&
                (exprReturnsEvalResult(VD->getInit()) || isEvalResultKindAccess(VD->getInit()))) {
                diag(VD->getLocation(), "Eval_Result must not be flattened to bool or integer state");
            }
            return;
        }

        if (const auto *BO = Result.Nodes.getNodeAs<BinaryOperator>("assign")) {
            llvm::StringRef Path = fileName(SM, BO->getOperatorLoc());
            if (!isEvaluatorPath(Path)) return;
            QualType LHSType = stripExpr(BO->getLHS())->getType();
            if ((LHSType->isBooleanType() || LHSType->isIntegerType()) &&
                (exprReturnsEvalResult(BO->getRHS()) || isEvalResultKindAccess(BO->getRHS()))) {
                diag(BO->getOperatorLoc(), "Eval_Result must not be flattened to bool or integer state");
            }
            return;
        }

        if (const auto *RS = Result.Nodes.getNodeAs<ReturnStmt>("return")) {
            llvm::StringRef Path = fileName(SM, RS->getBeginLoc());
            if (!isEvaluatorPath(Path)) return;
            const Expr *Ret = RS->getRetValue();
            if (Ret && isEvalResultKindAccess(Ret)) {
                diag(RS->getBeginLoc(), "Eval_Result must not be flattened to bool or integer return values");
            }
            return;
        }

        const auto *Call = Result.Nodes.getNodeAs<CallExpr>("call");
        if (!Call) return;
        llvm::StringRef Path = fileName(SM, Call->getExprLoc());
        if (!isEvaluatorPath(Path)) return;
        const FunctionDecl *Callee = Call->getDirectCallee();
        if (!functionReturnsEvalResult(Callee)) return;
        if (isEvalResultAllowedConsumer(Call)) return;

        ASTContext *Context = Result.Context;
        DynTypedNodeList Parents = Context->getParents(*Call);
        if (Parents.size() != 1) return;
        if (Parents[0].get<CompoundStmt>()) {
            diag(Call->getExprLoc(), "Eval_Result return value is discarded");
        }
    }

private:
    void collectEvalResultLocals(const Stmt *Node,
                                 llvm::SmallVectorImpl<const VarDecl *> &Locals,
                                 llvm::DenseMap<const VarDecl *, unsigned> &Uses) {
        if (!Node) return;
        if (const auto *DS = dyn_cast<DeclStmt>(Node)) {
            for (const Decl *D : DS->decls()) {
                const auto *VD = dyn_cast<VarDecl>(D);
                if (VD && VD->isLocalVarDecl() && typeNamesEvalResult(VD->getType())) {
                    Locals.push_back(VD);
                    Uses[VD] = 0;
                }
            }
        }
        for (const Stmt *Child : Node->children()) collectEvalResultLocals(Child, Locals, Uses);
    }

    void checkUnusedEvalResultLocal(ASTContext &, const FunctionDecl *FD) {
        if (!FD || !FD->hasBody()) return;
        llvm::SmallVector<const VarDecl *, 8> Locals;
        llvm::DenseMap<const VarDecl *, unsigned> Uses;
        collectEvalResultLocals(FD->getBody(), Locals, Uses);
        if (Locals.empty()) return;

        EvalResultLocalUseVisitor Visitor(Uses);
        Visitor.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
        for (const VarDecl *VD : Locals) {
            if (Uses.lookup(VD) == 0) {
                diag(VD->getLocation(),
                     "local Eval_Result must be returned, merged, or inspected explicitly");
            }
        }
    }
};

class StopProjectionVisitor : public RecursiveASTVisitor<StopProjectionVisitor> {
public:
    StopProjectionVisitor(ASTContext &Context, ClangTidyCheck &Check)
        : Context(Context), Check(Check) {}

    bool TraverseFunctionDecl(FunctionDecl *FD) {
        CurrentFunction = FD;
        Analyzer.reset();
        bool Result = RecursiveASTVisitor::TraverseFunctionDecl(FD);
        CurrentFunction = nullptr;
        return Result;
    }

    bool VisitVarDecl(VarDecl *VD) {
        Analyzer.collectAlias(VD);
        return true;
    }

    bool VisitReturnStmt(ReturnStmt *RS) {
        if (!CurrentFunction || !CurrentFunction->getReturnType()->isBooleanType()) return true;
        StopProjection Projection = Analyzer.classify(RS->getRetValue());
        if (Projection != StopProjection::Unknown) {
            Check.diag(RS->getBeginLoc(),
                       "bool return value must not be a direct projection of eval_should_stop(ctx)");
        }
        return true;
    }

    bool VisitCompoundStmt(CompoundStmt *CS) {
        if (!CurrentFunction || CurrentFunction->getBody() != CS ||
            !CurrentFunction->getReturnType()->isBooleanType()) {
            return true;
        }
        llvm::SmallVector<const Stmt *, 32> Children;
        for (const Stmt *Child : CS->body()) {
            if (isa<NullStmt>(Child)) continue;
            Children.push_back(Child);
        }
        for (size_t I = 0; I + 1 < Children.size(); ++I) {
            if (hasIndependentPrefix(Children, I)) continue;
            const auto *If = dyn_cast<IfStmt>(Children[I]);
            const auto *After = dyn_cast<ReturnStmt>(Children[I + 1]);
            if (!If || !After || If->getElse()) continue;
            const auto *ThenReturn = dyn_cast_or_null<ReturnStmt>(If->getThen());
            if (!ThenReturn) continue;
            StopProjection Cond = Analyzer.classify(If->getCond());
            if (Cond == StopProjection::Unknown) continue;
            bool ThenValue = false;
            bool AfterValue = false;
            if (!boolLiteralValue(ThenReturn->getRetValue(), &ThenValue) ||
                !boolLiteralValue(After->getRetValue(), &AfterValue) ||
                ThenValue == AfterValue) {
                continue;
            }
            Check.diag(If->getBeginLoc(),
                       "bool control flow must not encode success as only the inverse of eval_should_stop(ctx)");
        }
        return true;
    }

private:
    bool hasIndependentPrefix(const llvm::SmallVectorImpl<const Stmt *> &Children,
                              size_t BeforeIndex) const {
        for (size_t I = 0; I < BeforeIndex; ++I) {
            const Stmt *Child = Children[I];
            if (isa<NullStmt>(Child)) continue;
            const auto *DS = dyn_cast<DeclStmt>(Child);
            if (!DS) return true;
            for (const Decl *D : DS->decls()) {
                const auto *VD = dyn_cast<VarDecl>(D);
                if (!VD || !VD->hasInit() ||
                    Analyzer.classify(VD->getInit()) == StopProjection::Unknown) {
                    return true;
                }
            }
        }
        return false;
    }

    ASTContext &Context;
    ClangTidyCheck &Check;
    FunctionStopAnalyzer Analyzer;
    FunctionDecl *CurrentFunction = nullptr;
};

class EvalStopBoolProjectionCheck : public ClangTidyCheck {
public:
    EvalStopBoolProjectionCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("function");
        if (!FD || !FD->hasBody()) return;
        llvm::StringRef Path = fileName(*Result.SourceManager, FD->getLocation());
        if (!isEvaluatorPath(Path)) return;
        StopProjectionVisitor Visitor(*Result.Context, *this);
        Visitor.TraverseFunctionDecl(const_cast<FunctionDecl *>(FD));
    }
};

class HandlerShapeRefVisitor : public RecursiveASTVisitor<HandlerShapeRefVisitor> {
public:
    explicit HandlerShapeRefVisitor(const ValueDecl *Needle) : Needle(Needle) {}

    bool VisitDeclRefExpr(DeclRefExpr *Ref) {
        if (Ref && Ref->getDecl() == Needle) Found = true;
        return !Found;
    }

    bool found() const { return Found; }

private:
    const ValueDecl *Needle = nullptr;
    bool Found = false;
};

static bool exprReferencesDecl(const Expr *E, const ValueDecl *Decl) {
    if (!E || !Decl) return false;
    HandlerShapeRefVisitor Visitor(Decl);
    Visitor.TraverseStmt(const_cast<Expr *>(E));
    return Visitor.found();
}

static bool exprCallsEvalShouldStop(const Expr *E) {
    E = stripExpr(E);
    if (!E) return false;
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
        if (calleeNamed(Call, "eval_should_stop")) return true;
    }
    for (const Stmt *Child : E->children()) {
        if (const auto *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
            if (exprCallsEvalShouldStop(ChildExpr)) return true;
        }
    }
    return false;
}

static const ReturnStmt *singleReturnStmt(const Stmt *S) {
    S = S ? S->IgnoreContainers() : nullptr;
    if (const auto *RS = dyn_cast_or_null<ReturnStmt>(S)) return RS;
    const auto *CS = dyn_cast_or_null<CompoundStmt>(S);
    if (!CS || CS->size() != 1) return nullptr;
    return dyn_cast_or_null<ReturnStmt>(*CS->body_begin());
}

static bool isAllowedHandlerGuardReturn(const ReturnStmt *RS, const ParmVarDecl *CtxParam) {
    if (!RS) return false;
    const Expr *Ret = stripExpr(RS->getRetValue());
    const auto *Call = dyn_cast_or_null<CallExpr>(Ret);
    if (!Call) return false;
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee) return false;
    llvm::StringRef Name = Callee->getName();
    if (Name == "eval_result_fatal" || Name == "eval_result_ok") return true;
    if ((Name == "eval_result_from_ctx" || Name == "eval_result_ok_if_running") &&
        Call->getNumArgs() == 1) {
        return exprReferencesDecl(Call->getArg(0), CtxParam);
    }
    return false;
}

static bool isAllowedHandlerReturnExpr(const Expr *Ret) {
    Ret = stripExpr(Ret);
    if (!Ret) return false;
    if (const auto *Call = dyn_cast<CallExpr>(Ret)) {
        const FunctionDecl *Callee = Call->getDirectCallee();
        if (!Callee) return false;
        llvm::StringRef Name = Callee->getName();
        if (Name == "eval_result_from_ctx" || Name == "eval_result_fatal" ||
            Name == "eval_result_ok" || Name == "eval_result_ok_if_running" ||
            Name == "eval_result_merge") {
            return true;
        }
        return functionReturnsEvalResult(Callee);
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Ret)) {
        if (const auto *VD = dyn_cast<VarDecl>(Ref->getDecl())) {
            return typeNamesEvalResult(VD->getType());
        }
    }
    return false;
}

static bool isPureEvalResultDelegation(const CompoundStmt *Body) {
    if (!Body || Body->size() != 1) return false;
    const auto *RS = dyn_cast_or_null<ReturnStmt>(*Body->body_begin());
    if (!RS) return false;
    const auto *Call = dyn_cast_or_null<CallExpr>(stripExpr(RS->getRetValue()));
    const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
    if (!functionReturnsEvalResult(Callee)) return false;
    return !Callee->getName().starts_with("eval_result_");
}

class HandlerReturnVisitor : public RecursiveASTVisitor<HandlerReturnVisitor> {
public:
    explicit HandlerReturnVisitor(ClangTidyCheck &Check) : Check(Check) {}

    bool VisitReturnStmt(ReturnStmt *RS) {
        if (!RS || !RS->getRetValue()) return true;
        if (!isAllowedHandlerReturnExpr(RS->getRetValue())) {
            Check.diag(RS->getBeginLoc(),
                       "eval_handle_* must return Eval_Result via approved helpers, merge, local result, or direct Eval_Result delegation");
        }
        return true;
    }

private:
    ClangTidyCheck &Check;
};

class EvalHandlerShapeCheck : public ClangTidyCheck {
public:
    EvalHandlerShapeCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("function");
        if (!FD || !FD->hasBody() || !isEvalHandleFunction(FD)) return;
        llvm::StringRef Path = fileName(*Result.SourceManager, FD->getLocation());
        if (!isEvaluatorPath(Path)) return;

        if (!functionReturnsEvalResult(FD)) {
            diag(FD->getLocation(), "eval_handle_* functions must return Eval_Result");
            return;
        }
        if (FD->param_size() < 2 || FD->getParamDecl(0)->getName() != "ctx" ||
            FD->getParamDecl(1)->getName() != "node") {
            diag(FD->getLocation(),
                 "eval_handle_* functions must use the canonical (ctx, node) leading parameters");
        }

        const auto *CtxParam = FD->param_size() > 0 ? FD->getParamDecl(0) : nullptr;
        const auto *Body = dyn_cast<CompoundStmt>(FD->getBody());
        if ((!Body || !hasInitialGuard(Body, CtxParam)) && !isPureEvalResultDelegation(Body)) {
            diag(FD->getLocation(),
                 "eval_handle_* functions must begin with an explicit ctx/eval_should_stop guard or directly delegate to an Eval_Result handler helper");
        }

        HandlerReturnVisitor Visitor(*this);
        Visitor.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
    }

private:
    bool hasInitialGuard(const CompoundStmt *Body, const ParmVarDecl *CtxParam) const {
        if (!Body || !CtxParam) return false;
        unsigned Checked = 0;
        for (const Stmt *Child : Body->body()) {
            if (!Child || isa<NullStmt>(Child)) continue;
            if (isa<DeclStmt>(Child)) {
                if (++Checked >= 3) return false;
                continue;
            }
            const auto *If = dyn_cast<IfStmt>(Child);
            if (!If) return false;
            const Expr *Cond = If->getCond();
            if (!exprReferencesDecl(Cond, CtxParam) && !exprCallsEvalShouldStop(Cond)) {
                return false;
            }
            return isAllowedHandlerGuardReturn(singleReturnStmt(If->getThen()), CtxParam);
        }
        return false;
    }
};

static bool appliesEvaluatorStateOwnership(llvm::StringRef Path,
                                           ASTContext *Context,
                                           const MemberExpr *Member) {
    (void)Context;
    (void)Member;
    return isEvaluatorPath(Path) && !isEvaluatorOwnerFile(Path);
}

class EvaluatorStateOwnershipCheck : public ClangTidyCheck {
public:
    EvaluatorStateOwnershipCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerNamedFieldMutationMatcher(Finder, this, "^(oom|stop_requested)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Member_Access_Rule Rule = {
            appliesEvaluatorStateOwnership,
            recordIsEvalExecContext,
            "ctx->oom and ctx->stop_requested may only be written by evaluator.c",
        };
        checkMemberAccess(Result, *this, Rule, "member-mutation");
    }
};

static bool appliesBuildModelQueryFacade(llvm::StringRef Path,
                                         ASTContext *Context,
                                         const MemberExpr *Member) {
    (void)Context;
    (void)Member;
    return isProductionBuildModelConsumerPath(Path);
}

class BuildModelQueryBoundaryCheck : public ClangTidyCheck {
public:
    BuildModelQueryBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerMemberAccessMatcher(Finder, this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Member_Access_Rule Rule = {
            appliesBuildModelQueryFacade,
            recordIsBuildModelOrBMRecord,
            "production consumers must use bm_query_* instead of direct build-model record fields",
        };
        checkMemberAccess(Result, *this, Rule, "member-access");
    }
};

static bool appliesCodegenEventIrBoundary(llvm::StringRef Path, const FunctionDecl *FD) {
    (void)FD;
    return isCodegenBoundaryPath(Path);
}

class CodegenEventIrBoundaryCheck : public ClangTidyCheck {
public:
    CodegenEventIrBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "");
        std::string EventRegex = semanticNameRegex("^EVENT_");
        Finder->addMatcher(declRefExpr(to(enumConstantDecl(matchesName(EventRegex)))).bind("event-constant"),
                           this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("event-constant")) {
            llvm::StringRef Path = fileName(*Result.SourceManager, Ref->getExprLoc());
            if (!isCodegenBoundaryPath(Path)) return;
            diag(Ref->getExprLoc(),
                 "codegen must not branch on EVENT_*; preserve this semantic distinction upstream");
            return;
        }

        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesCodegenEventIrBoundary,
            typeNamesEventIr,
            "codegen must consume the frozen build model, not Event IR values",
            "codegen APIs must not accept Event IR; route semantics through build_model and bm_query_*",
            "codegen APIs must not return Event IR; expose frozen build-model query results instead",
            "",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesBuildModelCodegenDependency(llvm::StringRef Path, const FunctionDecl *FD) {
    (void)FD;
    return isBuildModelOwnerPath(Path) || isFixturePath(Path);
}

class BuildModelCodegenDependencyCheck : public ClangTidyCheck {
public:
    BuildModelCodegenDependencyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^nob_codegen_");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesBuildModelCodegenDependency,
            typeNamesCodegen,
            "build_model must not store codegen types; expose frozen facts through bm_query_*",
            "build_model APIs must not accept codegen types; keep codegen downstream of bm_query_*",
            "build_model APIs must not return codegen types; keep backend policy out of the frozen model",
            "build_model must not call nob_codegen_*; codegen consumes build_model, not the reverse",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesEvaluatorFileHostEnumeration(llvm::StringRef Path, const FunctionDecl *FD) {
    (void)FD;
    return isEvaluatorFileHandlerPath(Path);
}

class EvaluatorFileHostEnumerationCheck : public ClangTidyCheck {
public:
    EvaluatorFileHostEnumerationCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(
            Finder,
            this,
            "^(nob_read_entire_dir|opendir|readdir|closedir|tinydir_open|tinydir_readfile|tinydir_next|tinydir_close)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesEvaluatorFileHostEnumeration,
            "file() evaluator handlers must use centralized filesystem enumeration helpers instead of direct host directory walking",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool callIsEvaluatorPipelinePhase(const FunctionDecl *FD) {
    return FD && FD->getName().starts_with("eval_session_");
}

static bool callIsBuildModelLifecyclePipelinePhase(const FunctionDecl *FD) {
    if (!FD) return false;
    llvm::StringRef Name = FD->getName();
    return Name.starts_with("bm_builder_") || Name == "bm_validate_draft" ||
           Name == "bm_freeze_draft";
}

static bool callIsCodegenPipelinePhase(const FunctionDecl *FD) {
    return FD && FD->getName() == "nob_codegen_render";
}

static bool appliesPipelineOrchestrationBoundary(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isPipelineOrchestrationPath(Path)) return false;
    if (isFixturePath(Path)) {
        return FD && FD->getName().contains("pipeline_orchestration_boundary");
    }
    return contains(Path, "src_v2/");
}

class PipelineOrchestrationBoundaryCheck : public ClangTidyCheck {
public:
    PipelineOrchestrationBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerPhaseChainMatcher(Finder, this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const SemanticCallPhaseMatcherFn Phases[] = {
            callIsEvaluatorPipelinePhase,
            callIsBuildModelLifecyclePipelinePhase,
            callIsCodegenPipelinePhase,
        };
        static const Semantic_Phase_Chain_Rule Rule = {
            appliesPipelineOrchestrationBoundary,
            Phases,
            sizeof(Phases) / sizeof(Phases[0]),
            "only app or test orchestration code may chain evaluator, build_model lifecycle, and codegen phases",
        };
        checkPhaseChain(Result, *this, Rule);
    }
};

static bool appliesBuildModelConstructionQueryLayer(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isBuildModelConstructionPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    llvm::StringRef FunctionName = FD->getName();
    return FunctionName.contains("builder") || FunctionName.contains("freeze");
}

class BuildModelConstructionQueryLayerCheck : public ClangTidyCheck {
public:
    BuildModelConstructionQueryLayerCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(Finder, this, "^bm_query_");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesBuildModelConstructionQueryLayer,
            "build_model construction and freeze code must not call bm_query_*; use owned records before exposing the query layer",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesEvaluatorBuildModelDependency(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/evaluator/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("evaluator_build_model");
}

class EvaluatorBuildModelDependencyCheck : public ClangTidyCheck {
public:
    EvaluatorBuildModelDependencyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^bm_query_");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesEvaluatorBuildModelDependency,
            typeNamesFrozenBuildModel,
            "evaluator must emit Event IR instead of consuming the frozen Build_Model",
            "evaluator APIs must not accept Build_Model; keep build_model downstream of Event IR",
            "evaluator APIs must not return Build_Model; publish typed Event IR instead",
            "evaluator must not call bm_query_*; query the frozen model only after build_model freeze",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesCodegenEvaluatorDependency(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_evaluator");
}

class CodegenEvaluatorDependencyCheck : public ClangTidyCheck {
public:
    CodegenEvaluatorDependencyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^eval_");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesCodegenEvaluatorDependency,
            typeNamesEvaluatorState,
            "codegen must not store evaluator execution state; consume frozen build-model queries instead",
            "codegen APIs must not accept evaluator state; route semantics through Event IR and build_model",
            "codegen APIs must not return evaluator state; backend output comes from build-model facts",
            "codegen must not call eval_* APIs; do not re-enter evaluator semantics during backend generation",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesCodegenParserDependency(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_parser");
}

class CodegenParserDependencyCheck : public ClangTidyCheck {
public:
    CodegenParserDependencyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(parse_|parser_)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesCodegenParserDependency,
            typeNamesParserState,
            "codegen must not store parser or AST state; consume frozen build-model queries instead",
            "codegen APIs must not accept parser or AST state; route semantics through evaluator and build_model",
            "codegen APIs must not return parser or AST state",
            "codegen must not call parser APIs; backend generation starts from the frozen build model",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesParserDownstreamDependency(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isParserOrLexerPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("parser_downstream");
}

class ParserDownstreamDependencyCheck : public ClangTidyCheck {
public:
    ParserDownstreamDependencyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(event_|eval_|bm_|nob_codegen_)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesParserDownstreamDependency,
            typeNamesParserDownstreamState,
            "parser and lexer must not store downstream semantic state; produce syntax only",
            "parser and lexer APIs must not accept Event IR, evaluator, build_model, or codegen state",
            "parser and lexer APIs must not return downstream semantic state",
            "parser and lexer must not call evaluator, Event IR, build_model, or codegen APIs",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesEventIrDownstreamDependency(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isEventIrOwnerPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("event_ir_downstream");
}

class EventIrDownstreamDependencyCheck : public ClangTidyCheck {
public:
    EventIrDownstreamDependencyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(bm_|nob_codegen_)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesEventIrDownstreamDependency,
            typeNamesEventIrDownstreamState,
            "Event IR must not store build_model or codegen state; it is the evaluator-to-builder boundary",
            "Event IR APIs must not accept build_model or codegen state",
            "Event IR APIs must not return build_model or codegen state",
            "Event IR must not call build_model or codegen APIs",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesEvaluatorBuildModelLifecycle(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/evaluator/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("evaluator_build_model_lifecycle");
}

class EvaluatorBuildModelLifecycleCheck : public ClangTidyCheck {
public:
    EvaluatorBuildModelLifecycleCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(bm_builder_|builder_|bm_freeze|bm_validate)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesEvaluatorBuildModelLifecycle,
            typeNamesBuildModelLifecycle,
            "evaluator must not store build-model builder or draft state; emit Event IR instead",
            "evaluator APIs must not accept build-model builder or draft state; publish Event IR",
            "evaluator APIs must not return build-model builder or draft state",
            "evaluator must not drive build-model construction, validation, or freeze",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesCodegenBuildModelLifecycle(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_build_model_lifecycle");
}

class CodegenBuildModelLifecycleCheck : public ClangTidyCheck {
public:
    CodegenBuildModelLifecycleCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(bm_builder_|builder_|bm_freeze|bm_validate)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesCodegenBuildModelLifecycle,
            typeNamesBuildModelLifecycle,
            "codegen must not store build-model builder or draft state; consume frozen query APIs",
            "codegen APIs must not accept build-model builder or draft state; use frozen Build_Model queries",
            "codegen APIs must not return build-model builder or draft state",
            "codegen must not drive build-model construction, freeze, or validation",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesCodegenRenderHostEffect(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_render_host_effect") ||
           FD->getName().contains("codegen_path_resolution_host_effect");
}

class CodegenRenderHostEffectCheck : public ClangTidyCheck {
public:
    CodegenRenderHostEffectCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(
            Finder,
            this,
            "^(nob_file_exists|nob_get_file_type|nob_read_entire_dir|nob_write_entire_file|nob_read_entire_file|nob_cmd_run|system|popen|stat|lstat|access|realpath|fopen|open|remove|rename|unlink|rmdir)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesCodegenRenderHostEffect,
            "codegen render functions must not perform host effects; emit generated backend code or use an outer write wrapper",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesCodegenPublicHostEffect(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) {
        return FD && FD->getName().starts_with("nob_codegen_");
    }
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_public_host_effect");
}

class CodegenPublicHostEffectCheck : public ClangTidyCheck {
public:
    CodegenPublicHostEffectCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(
            Finder,
            this,
            "^(cg_host_ensure_dir|nob_mkdir_if_not_exists|nob_write_entire_file|nob_read_entire_file|nob_cmd_run|system|popen|fopen|open|remove|rename|unlink|rmdir)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesCodegenPublicHostEffect,
            "public codegen APIs must not perform host effects; render into a buffer and let the app or test harness write files",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesPureLayerAmbientEnv(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isPureSemanticLayerPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("pure_layer_ambient_env");
}

class PureLayerAmbientEnvCheck : public ClangTidyCheck {
public:
    PureLayerAmbientEnvCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(
            Finder,
            this,
            "^(getenv|secure_getenv|setenv|unsetenv|putenv|_putenv|_putenv_s)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesPureLayerAmbientEnv,
            "pure semantic layers must not read or mutate ambient environment; pass explicit options or use the owning runtime boundary",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesPureLayerHostEffect(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isPureSemanticLayerPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("pure_layer_host_effect");
}

class PureLayerHostEffectCheck : public ClangTidyCheck {
public:
    PureLayerHostEffectCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(
            Finder,
            this,
            "^(nob_file_exists|nob_get_file_type|nob_mkdir_if_not_exists|nob_walk_dir|nob_read_entire_dir|nob_read_entire_file|nob_write_entire_file|nob_copy_file|nob_cmd_run|system|popen|fork|vfork|execv|execve|execvp|execl|execlp|wait|waitpid|stat|lstat|access|realpath|getcwd|chdir|fopen|open|remove|rename|unlink|mkdir|rmdir)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesPureLayerHostEffect,
            "pure semantic layers must not perform host effects; pass observed facts through an explicit service or model boundary",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesPureLayerAmbientNondeterminism(llvm::StringRef Path,
                                                  const FunctionDecl *FD) {
    if (isPureSemanticLayerPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("pure_layer_ambient_nondeterminism");
}

class PureLayerAmbientNondeterminismCheck : public ClangTidyCheck {
public:
    PureLayerAmbientNondeterminismCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(
            Finder,
            this,
            "^(time|clock|gettimeofday|clock_gettime|localtime|localtime_r|gmtime|gmtime_r|mktime|rand|srand|random|srandom|arc4random|uuid_generate)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesPureLayerAmbientNondeterminism,
            "pure semantic layers must not read time, randomness, or other ambient nondeterminism",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesPureLayerMutableGlobalState(llvm::StringRef Path, const FunctionDecl *FD) {
    (void)FD;
    return isPureSemanticLayerPath(Path) || isFixturePath(Path);
}

class PureLayerMutableGlobalStateCheck : public ClangTidyCheck {
public:
    PureLayerMutableGlobalStateCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerGlobalStateMatcher(Finder, this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Global_State_Rule Rule = {
            appliesPureLayerMutableGlobalState,
            "pure semantic layers must not keep mutable global or static-local state",
        };
        checkGlobalState(Result, *this, Rule);
    }
};

static bool literalIsBuildStepToolHeuristic(llvm::StringRef Value) {
    return Value == "cmake" || Value == "cpack";
}

static bool appliesCodegenBuildStepToolHeuristic(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_build_step_tool_heuristic");
}

class CodegenBuildStepToolHeuristicCheck : public ClangTidyCheck {
public:
    CodegenBuildStepToolHeuristicCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerCallLiteralMatcher(Finder, this, "nob_sv_eq");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Call_Literal_Rule Rule = {
            appliesCodegenBuildStepToolHeuristic,
            literalIsBuildStepToolHeuristic,
            "codegen must not classify build-step tools by argv string comparison; use build-model command tool queries",
        };
        checkCallLiteral(Result, *this, Rule);
    }
};

static bool literalIsCPackGroupingHeuristic(llvm::StringRef Value) {
    return Value == "ONE_PER_GROUP" || Value == "IGNORE" || Value == "ALL_COMPONENTS_IN_ONE";
}

static bool appliesCodegenCPackGroupingHeuristic(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_cpack_grouping_heuristic");
}

class CodegenCPackGroupingHeuristicCheck : public ClangTidyCheck {
public:
    CodegenCPackGroupingHeuristicCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerCallLiteralMatcher(Finder, this, "nob_sv_eq");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Call_Literal_Rule Rule = {
            appliesCodegenCPackGroupingHeuristic,
            literalIsCPackGroupingHeuristic,
            "codegen must not interpret CPACK_COMPONENTS_GROUPING by string comparison; use build-model package grouping queries",
        };
        checkCallLiteral(Result, *this, Rule);
    }
};

static bool literalIsInstallPseudoItemHeuristic(llvm::StringRef Value) {
    return Value == "SCRIPT::" || Value == "CODE::" || Value == "EXPORT_ANDROID_MK::";
}

static bool appliesCodegenInstallPseudoItemHeuristic(llvm::StringRef Path,
                                                     const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_install_pseudo_item_heuristic");
}

class CodegenInstallPseudoItemHeuristicCheck : public ClangTidyCheck {
public:
    CodegenInstallPseudoItemHeuristicCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerCallLiteralMatcher(Finder, this, "cg_sv_has_prefix");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Call_Literal_Rule Rule = {
            appliesCodegenInstallPseudoItemHeuristic,
            literalIsInstallPseudoItemHeuristic,
            "codegen must not classify install pseudo-items by raw TAG:: prefixes; use build-model install item kind queries",
        };
        checkCallLiteral(Result, *this, Rule);
    }
};

static bool literalIsLanguageExtensionsRawProperty(llvm::StringRef Value) {
    return Value == "C_EXTENSIONS" || Value == "CXX_EXTENSIONS";
}

static bool appliesCodegenLanguageExtensionsRawProperty(llvm::StringRef Path,
                                                        const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_language_extensions_raw_property");
}

class CodegenLanguageExtensionsRawPropertyCheck : public ClangTidyCheck {
public:
    CodegenLanguageExtensionsRawPropertyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerCallLiteralMatcher(Finder, this, "bm_query_target_raw_property_items");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Call_Literal_Rule Rule = {
            appliesCodegenLanguageExtensionsRawProperty,
            literalIsLanguageExtensionsRawProperty,
            "codegen must not inspect raw C/CXX extension properties; use build-model language extension override queries",
        };
        checkCallLiteral(Result, *this, Rule);
    }
};

static bool literalIsPublicHeaderRawProperty(llvm::StringRef Value) {
    return Value == "PUBLIC_HEADER";
}

static bool appliesCodegenPublicHeaderRawProperty(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_public_header_raw_property");
}

class CodegenPublicHeaderRawPropertyCheck : public ClangTidyCheck {
public:
    CodegenPublicHeaderRawPropertyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerCallLiteralMatcher(Finder, this, "bm_query_target_raw_property_items");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Call_Literal_Rule Rule = {
            appliesCodegenPublicHeaderRawProperty,
            literalIsPublicHeaderRawProperty,
            "codegen must not inspect raw PUBLIC_HEADER target properties; use build-model public header queries",
        };
        checkCallLiteral(Result, *this, Rule);
    }
};

static bool appliesCodegenRawPropertyEscape(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/codegen/")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("codegen_raw_property_escape") ||
           FD->getName().contains("codegen_language_extensions_raw_property") ||
           FD->getName().contains("codegen_public_header_raw_property");
}

class CodegenRawPropertyEscapeCheck : public ClangTidyCheck {
public:
    CodegenRawPropertyEscapeCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(Finder, this, "^bm_query_.*raw_property");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesCodegenRawPropertyEscape,
            "codegen must not consume raw build-model properties directly; add a typed bm_query_* facade for the semantic fact",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesEvaluatorHostServiceBoundary(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isEvaluatorHostServiceConsumerPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("evaluator_host_service_boundary");
}

class EvaluatorHostServiceBoundaryCheck : public ClangTidyCheck {
public:
    EvaluatorHostServiceBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerForbiddenCallMatcher(
            Finder,
            this,
            "^(nob_file_exists|nob_get_file_type|nob_mkdir_if_not_exists|nob_walk_dir|nob_read_entire_dir|nob_read_entire_file|nob_write_entire_file|nob_copy_file|nob_cmd_run|stat|lstat|access|realpath|fopen|open|remove|rename|unlink|rmdir)$");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Forbidden_Call_Rule Rule = {
            appliesEvaluatorHostServiceBoundary,
            "evaluator command helpers must use eval_service_* or eval_process_* for host effects",
        };
        checkForbiddenCall(Result, *this, Rule);
    }
};

static bool appliesBuildModelQueryReadonly(llvm::StringRef Path,
                                           ASTContext *Context,
                                           const MemberExpr *Member) {
    if (isBuildModelQueryPath(Path)) return true;
    if (!isFixturePath(Path)) return false;
    return ancestorFunctionNameContains(Context, Member, "query_mutates");
}

class BuildModelQueryReadonlyCheck : public ClangTidyCheck {
public:
    BuildModelQueryReadonlyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerMemberMutationMatcher(Finder, this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Member_Access_Rule Rule = {
            appliesBuildModelQueryReadonly,
            recordIsFrozenBuildModelRecord,
            "build_model query code must not mutate frozen model records",
        };
        checkMemberAccess(Result, *this, Rule, "member-mutation");
    }
};

static bool appliesBuildModelQueryFrozenBoundary(llvm::StringRef Path, const FunctionDecl *FD) {
    if (isBuildModelQueryPath(Path)) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("query_frozen_boundary");
}

class BuildModelQueryFrozenBoundaryCheck : public ClangTidyCheck {
public:
    BuildModelQueryFrozenBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(bm_builder_|builder_|bm_freeze|bm_validate)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesBuildModelQueryFrozenBoundary,
            typeNamesBuildModelLifecycle,
            "build_model query code must not store builder or draft state; query frozen Build_Model records",
            "build_model query APIs must not accept builder or draft state; accept frozen Build_Model",
            "build_model query APIs must not return builder or draft state",
            "build_model query code must not drive builder, validation, or freeze lifecycle",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesBuildModelValidateReadonly(llvm::StringRef Path,
                                              ASTContext *Context,
                                              const MemberExpr *Member) {
    if (isBuildModelValidatePath(Path)) return true;
    if (!isFixturePath(Path)) return false;
    return ancestorFunctionNameContains(Context, Member, "validate_mutates");
}

class BuildModelValidateReadonlyCheck : public ClangTidyCheck {
public:
    BuildModelValidateReadonlyCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerMemberMutationMatcher(Finder, this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Member_Access_Rule Rule = {
            appliesBuildModelValidateReadonly,
            recordIsDraftOrFrozenBuildModelRecord,
            "build_model validation must report invalid state without mutating draft or model records",
        };
        checkMemberAccess(Result, *this, Rule, "member-mutation");
    }
};

static bool appliesBuildModelValidateDraftBoundary(llvm::StringRef Path, const FunctionDecl *FD) {
    if (Path.ends_with("src_v2/build_model/build_model_validate.c")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("validate_draft_boundary");
}

class BuildModelValidateDraftBoundaryCheck : public ClangTidyCheck {
public:
    BuildModelValidateDraftBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(bm_query_|bm_builder_|builder_|bm_freeze)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesBuildModelValidateDraftBoundary,
            typeNamesFrozenModelOrBuilderState,
            "build_model draft validation must not store frozen model or builder state",
            "build_model draft validation APIs must not accept frozen model or builder state",
            "build_model draft validation APIs must not return frozen model or builder state",
            "build_model draft validation must not call query, builder, or freeze APIs",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesBuildModelFreezeBuilderBoundary(llvm::StringRef Path, const FunctionDecl *FD) {
    if (Path.ends_with("src_v2/build_model/build_model_freeze.c")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("freeze_builder");
}

class BuildModelFreezeBuilderBoundaryCheck : public ClangTidyCheck {
public:
    BuildModelFreezeBuilderBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(bm_builder_|builder_)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesBuildModelFreezeBuilderBoundary,
            typeNamesBuildModelBuilderState,
            "build_model freeze must not store builder state; freeze consumes the finalized draft",
            "build_model freeze APIs must not accept builder state; accept Build_Model_Draft instead",
            "build_model freeze APIs must not return builder state",
            "build_model freeze must not call builder APIs after the draft is finalized",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

static bool appliesBuildModelBuilderUpstreamBoundary(llvm::StringRef Path, const FunctionDecl *FD) {
    if (contains(Path, "src_v2/build_model/build_model_builder")) return true;
    if (!isFixturePath(Path) || !FD) return false;
    return FD->getName().contains("builder_upstream");
}

class BuildModelBuilderUpstreamBoundaryCheck : public ClangTidyCheck {
public:
    BuildModelBuilderUpstreamBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        registerLayerDependencyMatchers(Finder, this, "^(eval_|parse_)");
    }

    void check(const MatchFinder::MatchResult &Result) override {
        static const Semantic_Layer_Dependency_Rule Rule = {
            appliesBuildModelBuilderUpstreamBoundary,
            typeNamesEvaluatorOrParserState,
            "build_model builder must consume Event IR instead of evaluator or parser state",
            "build_model builder APIs must not accept evaluator or parser state; use Event IR",
            "build_model builder APIs must not return evaluator or parser state",
            "build_model builder must not call evaluator or parser APIs; consume Event IR",
        };
        checkLayerDependency(Result, *this, Rule);
    }
};

class ArenaLifetimeVisitor : public RecursiveASTVisitor<ArenaLifetimeVisitor> {
public:
    ArenaLifetimeVisitor(ASTContext &Context, ClangTidyCheck &Check)
        : Context(Context), Check(Check) {}

    bool VisitVarDecl(VarDecl *VD) {
        if (VD && VD->hasInit() && isTempOrigin(VD->getInit())) Tainted[VD] = true;
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator *BO) {
        if (!BO || !BO->isAssignmentOp()) return true;
        const Expr *LHS = stripExpr(BO->getLHS());
        const Expr *RHS = stripExpr(BO->getRHS());
        if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(LHS)) {
            if (const auto *VD = dyn_cast<VarDecl>(Ref->getDecl())) {
                if (isTempOrigin(RHS)) Tainted[VD] = true;
            }
        }
        if (isPersistentSink(LHS) && isTempOrigin(RHS) && !isApprovedPersistentCopy(RHS)) {
            Check.diag(BO->getOperatorLoc(),
                       "temporary arena value escapes to persistent state without an approved copy helper");
        }
        return true;
    }

private:
    bool isTempOrigin(const Expr *E) const {
        E = stripExpr(E);
        if (!E) return false;
        if (const auto *Call = dyn_cast<CallExpr>(E)) {
            const FunctionDecl *FD = Call->getDirectCallee();
            if (!FD) return false;
            llvm::StringRef Name = FD->getName();
            if (isApprovedPersistentCopy(E)) return false;
            return Name.starts_with("nob_temp_") || Name.contains("_temp") ||
                   Name == "eval_temp_arena";
        }
        if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
            const auto *VD = dyn_cast<VarDecl>(Ref->getDecl());
            return VD && Tainted.lookup(VD);
        }
        if (const auto *Cond = dyn_cast<ConditionalOperator>(E)) {
            return isTempOrigin(Cond->getTrueExpr()) || isTempOrigin(Cond->getFalseExpr());
        }
        return false;
    }

    bool isApprovedPersistentCopy(const Expr *E) const {
        E = stripExpr(E);
        const auto *Call = dyn_cast_or_null<CallExpr>(E);
        const FunctionDecl *FD = Call ? Call->getDirectCallee() : nullptr;
        if (!FD) return false;
        llvm::StringRef Name = FD->getName();
        return Name.contains("copy_to") || Name.contains("_copy_") ||
               Name.contains("intern") || Name.contains("transfer");
    }

    bool isPersistentSink(const Expr *E) const {
        E = stripExpr(E);
        const auto *Member = dyn_cast_or_null<MemberExpr>(E);
        if (!Member) return false;
        std::string Text = sourceText(Context, Member->getSourceRange());
        return llvm::StringRef(Text).contains("semantic_state") ||
               llvm::StringRef(Text).contains("event") ||
               llvm::StringRef(Text).contains("builder");
    }

    ASTContext &Context;
    ClangTidyCheck &Check;
    llvm::DenseMap<const VarDecl *, bool> Tainted;
};

class ArenaLifetimeCheck : public ClangTidyCheck {
public:
    ArenaLifetimeCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("function");
        if (!FD || !FD->hasBody()) return;
        llvm::StringRef Path = fileName(*Result.SourceManager, FD->getLocation());
        if (!contains(Path, "src_v2/") && !isFixturePath(Path)) return;
        ArenaLifetimeVisitor Visitor(*Result.Context, *this);
        Visitor.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
    }
};

class NobifySemanticModule : public ClangTidyModule {
public:
    void addCheckFactories(ClangTidyCheckFactories &Factories) override {
        Factories.registerCheck<EvalResultPropagationCheck>(
            "nobify-eval-result-propagation");
        Factories.registerCheck<EvalStopBoolProjectionCheck>(
            "nobify-eval-stop-bool-projection");
        Factories.registerCheck<EvalHandlerShapeCheck>(
            "nobify-eval-handler-shape");
        Factories.registerCheck<EvaluatorStateOwnershipCheck>(
            "nobify-evaluator-state-ownership");
        Factories.registerCheck<BuildModelQueryBoundaryCheck>(
            "nobify-build-model-query-boundary");
        Factories.registerCheck<CodegenEventIrBoundaryCheck>(
            "nobify-codegen-event-ir-boundary");
        Factories.registerCheck<BuildModelCodegenDependencyCheck>(
            "nobify-build-model-codegen-dependency");
        Factories.registerCheck<EvaluatorFileHostEnumerationCheck>(
            "nobify-evaluator-file-host-enumeration");
        Factories.registerCheck<PipelineOrchestrationBoundaryCheck>(
            "nobify-pipeline-orchestration-boundary");
        Factories.registerCheck<BuildModelConstructionQueryLayerCheck>(
            "nobify-build-model-construction-query-layer");
        Factories.registerCheck<EvaluatorBuildModelDependencyCheck>(
            "nobify-evaluator-build-model-dependency");
        Factories.registerCheck<CodegenEvaluatorDependencyCheck>(
            "nobify-codegen-evaluator-dependency");
        Factories.registerCheck<CodegenParserDependencyCheck>(
            "nobify-codegen-parser-dependency");
        Factories.registerCheck<ParserDownstreamDependencyCheck>(
            "nobify-parser-downstream-dependency");
        Factories.registerCheck<EventIrDownstreamDependencyCheck>(
            "nobify-event-ir-downstream-dependency");
        Factories.registerCheck<EvaluatorBuildModelLifecycleCheck>(
            "nobify-evaluator-build-model-lifecycle");
        Factories.registerCheck<CodegenBuildModelLifecycleCheck>(
            "nobify-codegen-build-model-lifecycle");
        Factories.registerCheck<CodegenRenderHostEffectCheck>(
            "nobify-codegen-render-host-effect");
        Factories.registerCheck<CodegenPublicHostEffectCheck>(
            "nobify-codegen-public-host-effect");
        Factories.registerCheck<PureLayerAmbientEnvCheck>(
            "nobify-pure-layer-ambient-env");
        Factories.registerCheck<PureLayerHostEffectCheck>(
            "nobify-pure-layer-host-effect");
        Factories.registerCheck<PureLayerAmbientNondeterminismCheck>(
            "nobify-pure-layer-ambient-nondeterminism");
        Factories.registerCheck<PureLayerMutableGlobalStateCheck>(
            "nobify-pure-layer-mutable-global-state");
        Factories.registerCheck<CodegenBuildStepToolHeuristicCheck>(
            "nobify-codegen-build-step-tool-heuristic");
        Factories.registerCheck<CodegenCPackGroupingHeuristicCheck>(
            "nobify-codegen-cpack-grouping-heuristic");
        Factories.registerCheck<CodegenInstallPseudoItemHeuristicCheck>(
            "nobify-codegen-install-pseudo-item-heuristic");
        Factories.registerCheck<CodegenLanguageExtensionsRawPropertyCheck>(
            "nobify-codegen-language-extensions-raw-property");
        Factories.registerCheck<CodegenPublicHeaderRawPropertyCheck>(
            "nobify-codegen-public-header-raw-property");
        Factories.registerCheck<CodegenRawPropertyEscapeCheck>(
            "nobify-codegen-raw-property-escape");
        Factories.registerCheck<EvaluatorHostServiceBoundaryCheck>(
            "nobify-evaluator-host-service-boundary");
        Factories.registerCheck<BuildModelQueryReadonlyCheck>(
            "nobify-build-model-query-readonly");
        Factories.registerCheck<BuildModelQueryFrozenBoundaryCheck>(
            "nobify-build-model-query-frozen-boundary");
        Factories.registerCheck<BuildModelValidateReadonlyCheck>(
            "nobify-build-model-validate-readonly");
        Factories.registerCheck<BuildModelValidateDraftBoundaryCheck>(
            "nobify-build-model-validate-draft-boundary");
        Factories.registerCheck<BuildModelFreezeBuilderBoundaryCheck>(
            "nobify-build-model-freeze-builder-boundary");
        Factories.registerCheck<BuildModelBuilderUpstreamBoundaryCheck>(
            "nobify-build-model-builder-upstream-boundary");
        Factories.registerCheck<ArenaLifetimeCheck>("nobify-arena-lifetime");
    }
};

static ClangTidyModuleRegistry::Add<NobifySemanticModule>
    X("nobify-semantic-module", "Adds Nobify semantic architecture checks.");

volatile int NobifySemanticModuleAnchorSource = 0;

} // namespace clang::tidy::nobify
