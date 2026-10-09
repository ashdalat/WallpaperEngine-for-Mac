#include <rstd/test/gtest.hpp>

import rstd;
import wescene.pkg.parse;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace owe::shader_lex;

TEST(ShaderLex, CharClass) {
    EXPECT_TRUE(IsHSpace(' '));
    EXPECT_TRUE(IsHSpace('\t'));
    EXPECT_FALSE(IsHSpace('\n'));
    EXPECT_TRUE(IsVSpace('\n'));
    EXPECT_TRUE(IsVSpace('\r'));
    EXPECT_FALSE(IsVSpace(' '));
    EXPECT_TRUE(IsIdStart('_'));
    EXPECT_TRUE(IsIdStart('a'));
    EXPECT_TRUE(IsIdStart('Z'));
    EXPECT_FALSE(IsIdStart('9'));
    EXPECT_FALSE(IsIdStart('-'));
    EXPECT_TRUE(IsIdCont('0'));
    EXPECT_TRUE(IsIdCont('_'));
    EXPECT_TRUE(IsDigit('0'));
    EXPECT_TRUE(IsDigit('9'));
    EXPECT_FALSE(IsDigit('a'));
}

TEST(ShaderLex, SkipHSpaceDoesNotCrossNewline) {
    Cursor c("  \t  \n   abc"_str);
    c.SkipHSpace();
    EXPECT_EQ(c.Pos().to_primitive(), 5u);
    EXPECT_EQ(c.Peek(), '\n');
}

TEST(ShaderLex, ReadIdent) {
    {
        Cursor c("abc123 rest"_str);
        auto   id = c.ReadIdent();
        ASSERT_TRUE(id);
        EXPECT_EQ(*id, "abc123"_str);
    }
    {
        Cursor c("_x"_str);
        auto   id = c.ReadIdent();
        ASSERT_TRUE(id);
        EXPECT_EQ(*id, "_x"_str);
    }
    {
        Cursor c("9bad"_str);
        EXPECT_TRUE(c.ReadIdent().is_none());
        EXPECT_EQ(c.Pos().to_primitive(), 0u);
    }
    {
        Cursor c(""_str);
        EXPECT_TRUE(c.ReadIdent().is_none());
    }
}

TEST(ShaderLex, MatchKeywordRespectsIdentBoundary) {
    {
        Cursor c("uniform vec4"_str);
        EXPECT_TRUE(c.MatchKeyword("uniform"_str));
        EXPECT_EQ(c.Pos().to_primitive(), 7u);
    }
    {
        Cursor c("uniformly"_str);
        EXPECT_FALSE(c.MatchKeyword("uniform"_str));
        EXPECT_EQ(c.Pos().to_primitive(), 0u);
    }
    {
        Cursor c("uniform_x"_str);
        EXPECT_FALSE(c.MatchKeyword("uniform"_str));
    }
    {
        Cursor c("uniform"_str);
        EXPECT_TRUE(c.MatchKeyword("uniform"_str));
    }
}

TEST(ShaderLex, ReadArraySuffix) {
    {
        Cursor c("[42] rest"_str);
        auto   a = c.ReadArraySuffix();
        ASSERT_TRUE(a);
        EXPECT_EQ(*a, "[42]"_str);
        EXPECT_EQ(c.Pos().to_primitive(), 4u);
    }
    {
        Cursor c("[]"_str);
        auto   a = c.ReadArraySuffix();
        ASSERT_TRUE(a);
        EXPECT_EQ(*a, "[]"_str);
    }
    {
        // Bracket content is not validated by ReadArraySuffix.
        Cursor c("[N+1]"_str);
        auto   a = c.ReadArraySuffix();
        ASSERT_TRUE(a);
        EXPECT_EQ(*a, "[N+1]"_str);
    }
    {
        Cursor c("nope"_str);
        EXPECT_TRUE(c.ReadArraySuffix().is_none());
        EXPECT_EQ(c.Pos().to_primitive(), 0u);
    }
}

TEST(ShaderLex, SkipAllTriviaEatsBlockComment) {
    {
        Cursor c("/* abc */xy"_str);
        c.SkipAllTrivia();
        EXPECT_EQ(c.Pos().to_primitive(), 9u);
        EXPECT_EQ(c.Peek(), 'x');
    }
    {
        Cursor c("  /* a\n b */ z"_str);
        c.SkipAllTrivia();
        EXPECT_EQ(c.Peek(), 'z');
    }
    {
        Cursor c("// trailing"_str);
        c.SkipAllTrivia();
        EXPECT_TRUE(c.Eof());
    }
}

TEST(ShaderLex, MatchHashDirective) {
    {
        Cursor c("#include \"x\""_str);
        EXPECT_TRUE(c.MatchHashDirective("include"_str));
        // cursor sits after the directive name
        EXPECT_EQ(c.Peek(), ' ');
    }
    {
        Cursor c("  #  include \"x\""_str);
        EXPECT_TRUE(c.MatchHashDirective("include"_str));
    }
    {
        Cursor c("# require X"_str);
        EXPECT_TRUE(c.MatchHashDirective("require"_str));
    }
    {
        Cursor c("//#include \"x\""_str);
        EXPECT_FALSE(c.MatchHashDirective("include"_str));
        EXPECT_EQ(c.Pos().to_primitive(), 0u);
    }
}

TEST(ShaderLex, LineWalkerBasic) {
    ref<str>   src = "line1\nline2\nline3"_str;
    LineWalker w(src);
    ASSERT_FALSE(w.Done());
    EXPECT_EQ(w.Line(), "line1"_str);
    EXPECT_EQ(w.LineStart().to_primitive(), 0u);
    EXPECT_EQ(w.LineEnd().to_primitive(), 5u);
    w.Step();
    EXPECT_EQ(w.Line(), "line2"_str);
    w.Step();
    EXPECT_EQ(w.Line(), "line3"_str);
    w.Step();
    EXPECT_TRUE(w.Done());
}

TEST(ShaderLex, LineWalkerEmptyLines) {
    LineWalker w("a\n\nb"_str);
    EXPECT_EQ(w.Line(), "a"_str);
    w.Step();
    EXPECT_EQ(w.Line(), ""_str);
    w.Step();
    EXPECT_EQ(w.Line(), "b"_str);
    w.Step();
    EXPECT_TRUE(w.Done());
}

TEST(ShaderLex, LineWalkerMasksBlockCommentLines) {
    // The `uniform` inside a multi-line block comment must not be visible to
    // LineWalker::Line() so annotation collectors skip it.
    ref<str>   src = "alpha\n"
                     "/* hidden\n"
                     " uniform vec4 g_X;\n"
                     "*/\n"
                     "after"_str;
    LineWalker w(src);
    EXPECT_EQ(w.Line(), "alpha"_str);
    w.Step();
    // Line that opens block — still visible (content before `/*`).
    EXPECT_TRUE(w.Line().find("uniform"_str).is_none());
    w.Step();
    // Inside the block comment — masked.
    EXPECT_TRUE(w.Line().is_empty());
    w.Step();
    // `*/` ends the block; content after is visible (just `*/` itself).
    w.Step();
    EXPECT_EQ(w.Line(), "after"_str);
}

TEST(ShaderLex, LexerKinds) {
    Lexer lx("  uniform vec4 g_X[3]; // tail\n#include \"x.h\"\n/* blk */"_str);
    auto  t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::HSpace);
    EXPECT_EQ(t.text, "  "_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Ident);
    EXPECT_EQ(t.text, "uniform"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::HSpace);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Ident);
    EXPECT_EQ(t.text, "vec4"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::HSpace);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Ident);
    EXPECT_EQ(t.text, "g_X"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Punct);
    EXPECT_EQ(t.text, "["_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Int);
    EXPECT_EQ(t.text, "3"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Punct);
    EXPECT_EQ(t.text, "]"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Punct);
    EXPECT_EQ(t.text, ";"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::HSpace);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::LineComment);
    EXPECT_EQ(t.text, "// tail"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Newline);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Hash);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Ident);
    EXPECT_EQ(t.text, "include"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::HSpace);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::String);
    EXPECT_EQ(t.text, "\"x.h\""_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Newline);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::BlockComment);
    EXPECT_EQ(t.text, "/* blk */"_str);
    t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::Eof);
}

TEST(ShaderLex, LexerPeekDoesNotAdvance) {
    Lexer lx("abc def"_str);
    auto  p1 = lx.Peek();
    auto  p2 = lx.Peek();
    EXPECT_EQ(p1.kind, p2.kind);
    EXPECT_EQ(p1.offset, p2.offset);
    EXPECT_EQ(p1.text, "abc"_str);
    auto n = lx.Next();
    EXPECT_EQ(n.text, "abc"_str);
    EXPECT_EQ(lx.Peek().kind, TokenKind::HSpace);
}

TEST(ShaderLex, LexerSaveRestore) {
    Lexer lx("alpha beta"_str);
    (void)lx.Next(); // alpha
    auto s = lx.Save();
    auto t = lx.Next(); // HSpace
    EXPECT_EQ(t.kind, TokenKind::HSpace);
    lx.Restore(s);
    EXPECT_EQ(lx.Next().kind, TokenKind::HSpace);
}

TEST(ShaderLex, LexerUnterminatedStringTerminatesAtEol) {
    Lexer lx("\"oops\n"_str);
    auto  t = lx.Next();
    EXPECT_EQ(t.kind, TokenKind::String);
    EXPECT_EQ(t.text, "\"oops"_str); // no closing quote consumed
    EXPECT_EQ(lx.Next().kind, TokenKind::Newline);
}

TEST(ShaderLex, LexerKeepsUnknownUtf8OnCharacterBoundaries) {
    Lexer lx("left；right"_str);

    EXPECT_EQ(lx.Next().text, "left"_str);
    auto unknown = lx.Next();
    EXPECT_EQ(unknown.kind, TokenKind::Unknown);
    EXPECT_EQ(unknown.text, "；"_str);
    EXPECT_EQ(lx.Next().text, "right"_str);
    EXPECT_EQ(lx.Next().kind, TokenKind::Eof);
}

TEST(ShaderLex, ClassifyPreproc) {
    auto cls = [](ref<str> s) {
        return ClassifyPreproc(Cursor(s));
    };
    EXPECT_EQ(cls("#if X"_str), PpKind::If);
    EXPECT_EQ(cls("#ifdef X"_str), PpKind::Ifdef);
    EXPECT_EQ(cls("  # endif"_str), PpKind::Endif);
    EXPECT_EQ(cls("#define X 1"_str), PpKind::Define);
    EXPECT_EQ(cls("#require X"_str), PpKind::Require);
    EXPECT_EQ(cls("#include \"x\""_str), PpKind::Include);
    EXPECT_EQ(cls("int x;"_str), PpKind::None);
    EXPECT_EQ(cls("#unknown"_str), PpKind::Other);
}
