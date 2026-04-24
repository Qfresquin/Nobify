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

static bool isEvaluatorOwnerFile(llvm::StringRef Path) {
    return Path.ends_with("src_v2/evaluator/evaluator.c");
}

static bool typeNamesEvalResult(QualType Type) {
    if (Type.isNull()) return false;
    std::string Name = Type.getAsString();
    return Name == "Eval_Result" || Name == "struct Eval_Result" ||
           llvm::StringRef(Name).ends_with(" Eval_Result");
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

class EvaluatorStateOwnershipCheck : public ClangTidyCheck {
public:
    EvaluatorStateOwnershipCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        auto StateMember = memberExpr(member(fieldDecl(anyOf(hasName("oom"),
                                                              hasName("stop_requested")))))
                               .bind("member");
        Finder->addMatcher(binaryOperator(isAssignmentOperator(),
                                          hasLHS(ignoringParenImpCasts(StateMember))),
                           this);
        Finder->addMatcher(unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--")),
                                         hasUnaryOperand(ignoringParenImpCasts(StateMember))),
                           this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *Member = Result.Nodes.getNodeAs<MemberExpr>("member");
        if (!Member) return;
        const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
        const RecordDecl *Record = Field ? Field->getParent() : nullptr;
        if (!Record || Record->getName() != "EvalExecContext") return;
        llvm::StringRef Path = fileName(*Result.SourceManager, Member->getExprLoc());
        if (!isEvaluatorPath(Path) || isEvaluatorOwnerFile(Path)) return;
        diag(Member->getExprLoc(),
             "ctx->oom and ctx->stop_requested may only be written by evaluator.c");
    }
};

class BuildModelQueryBoundaryCheck : public ClangTidyCheck {
public:
    BuildModelQueryBoundaryCheck(StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(memberExpr().bind("member"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *Member = Result.Nodes.getNodeAs<MemberExpr>("member");
        if (!Member) return;
        llvm::StringRef Path = fileName(*Result.SourceManager, Member->getExprLoc());
        if (!isProductionBuildModelConsumerPath(Path)) return;
        const FieldDecl *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
        if (!Field) return;
        const RecordDecl *Record = Field->getParent();
        if (!Record) return;
        llvm::StringRef Name = Record->getName();
        if (Name == "Build_Model" || Name.starts_with("BM_")) {
            diag(Member->getExprLoc(),
                 "production consumers must use bm_query_* instead of direct build-model record fields");
        }
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
        Factories.registerCheck<ArenaLifetimeCheck>("nobify-arena-lifetime");
    }
};

static ClangTidyModuleRegistry::Add<NobifySemanticModule>
    X("nobify-semantic-module", "Adds Nobify semantic architecture checks.");

volatile int NobifySemanticModuleAnchorSource = 0;

} // namespace clang::tidy::nobify
