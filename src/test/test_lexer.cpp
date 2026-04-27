// Claw Compiler - Lexer Unit Tests (Fixed for TokenType API)

#include "test/test.h"
#include "lexer/lexer.h"

using namespace claw;
using namespace claw::lexer;

// Helper to run lexer on input and return tokens
std::vector<Token> tokenize(const std::string& input) {
    Lexer lexer(input);
    std::vector<Token> tokens;
    Token token;
    do {
        token = lexer.next_token();
        tokens.push_back(token);
    } while (token.type != TokenType::EndOfFile && token.type != TokenType::Invalid);
    return tokens;
}

CLAW_TEST_SUITE(Lexer);

CLAW_TEST(keywords_recognized) {
    std::vector<Token> tokens = tokenize("fn let if else match for while loop return break continue serial process");
    
    CLAW_ASSERT(tokens.size() >= 12);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Kw_fn);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Kw_let);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Kw_if);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::Kw_else);
    CLAW_ASSERT_EQ(tokens[4].type, TokenType::Kw_match);
    CLAW_ASSERT_EQ(tokens[5].type, TokenType::Kw_for);
    CLAW_ASSERT_EQ(tokens[6].type, TokenType::Kw_while);
    CLAW_ASSERT_EQ(tokens[7].type, TokenType::Kw_loop);
    CLAW_ASSERT_EQ(tokens[8].type, TokenType::Kw_return);
    CLAW_ASSERT_EQ(tokens[9].type, TokenType::Kw_break);
    CLAW_ASSERT_EQ(tokens[10].type, TokenType::Kw_continue);
    CLAW_ASSERT_EQ(tokens[11].type, TokenType::Kw_serial);
    
    return TestStatus::Pass;
}

CLAW_TEST(integers_parsed) {
    std::vector<Token> tokens = tokenize("42 0 123456 0xFF 0b101 0o77");
    
    CLAW_ASSERT(tokens.size() >= 7);  // 6 numbers + End
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::IntegerLiteral);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::IntegerLiteral);
    
    // Check hex
    CLAW_ASSERT(tokens[3].type == TokenType::IntegerLiteral);
    // Check binary
    CLAW_ASSERT(tokens[4].type == TokenType::IntegerLiteral);
    // Check octal
    CLAW_ASSERT(tokens[5].type == TokenType::IntegerLiteral);
    
    return TestStatus::Pass;
}

CLAW_TEST(floats_parsed) {
    std::vector<Token> tokens = tokenize("3.14 0.5 .5 3. 1e10 1.5e-5");
    
    CLAW_ASSERT(tokens.size() >= 7);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::FloatLiteral);
    
    auto& val0 = std::get<double>(tokens[0].value);
    CLAW_ASSERT_EQ(val0, 3.14);
    
    return TestStatus::Pass;
}

CLAW_TEST(strings_parsed) {
    std::vector<Token> tokens = tokenize("\"hello\" \"world\" \"multi\\nline\"");
    
    CLAW_ASSERT(tokens.size() >= 4);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::StringLiteral);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::StringLiteral);
    
    auto& str0 = std::get<std::string>(tokens[0].value);
    CLAW_ASSERT_EQ(str0, "hello");
    
    return TestStatus::Pass;
}

CLAW_TEST(identifiers_parsed) {
    std::vector<Token> tokens = tokenize("foo bar _private camelCase snake_case");
    
    CLAW_ASSERT(tokens.size() >= 6);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    
    auto& ident0 = std::get<std::string>(tokens[0].value);
    CLAW_ASSERT_EQ(ident0, "foo");
    
    return TestStatus::Pass;
}

CLAW_TEST(operators_recognized) {
    std::vector<Token> tokens = tokenize("+ - * / % = == != < > <= >= && || ! & | ^ ~ << >>");
    
    CLAW_ASSERT(tokens.size() >= 20);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Op_plus);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Op_minus);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Op_star);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::Op_slash);
    CLAW_ASSERT_EQ(tokens[4].type, TokenType::Op_percent);
    CLAW_ASSERT_EQ(tokens[5].type, TokenType::Op_eq_assign);
    CLAW_ASSERT_EQ(tokens[6].type, TokenType::Op_eq);
    CLAW_ASSERT_EQ(tokens[7].type, TokenType::Op_neq);
    CLAW_ASSERT_EQ(tokens[8].type, TokenType::Op_lt);
    CLAW_ASSERT_EQ(tokens[9].type, TokenType::Op_gt);
    CLAW_ASSERT_EQ(tokens[10].type, TokenType::Op_lte);
    CLAW_ASSERT_EQ(tokens[11].type, TokenType::Op_gte);
    CLAW_ASSERT_EQ(tokens[12].type, TokenType::Op_and);
    CLAW_ASSERT_EQ(tokens[13].type, TokenType::Op_or);
    
    return TestStatus::Pass;
}

CLAW_TEST(delimiters_recognized) {
    std::vector<Token> tokens = tokenize("( ) { } [ ] , . ; : -> =>");
    
    CLAW_ASSERT(tokens.size() >= 12);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::LParen);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::RParen);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::LBrace);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::RBrace);
    CLAW_ASSERT_EQ(tokens[4].type, TokenType::LBracket);
    CLAW_ASSERT_EQ(tokens[5].type, TokenType::RBracket);
    
    return TestStatus::Pass;
}

CLAW_TEST(single_line_comment) {
    std::vector<Token> tokens = tokenize("let x // this is a comment\nlet y");
    
    // Should parse: let, identifier, let, identifier, End
    CLAW_ASSERT(tokens.size() >= 5);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Kw_let);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    // Comment should be skipped
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Kw_let);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::Identifier);
    
    return TestStatus::Pass;
}

CLAW_TEST(multi_line_comment) {
    std::vector<Token> tokens = tokenize("let x /* multi\nline\ncomment */ let y");
    
    CLAW_ASSERT(tokens.size() >= 5);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Kw_let);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Kw_let);
    
    return TestStatus::Pass;
}

CLAW_TEST(nested_comments) {
    std::vector<Token> tokens = tokenize("let /* outer /* inner */ outer */ x");
    
    CLAW_ASSERT(tokens.size() >= 3);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Kw_let);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    
    return TestStatus::Pass;
}

CLAW_TEST(source_location_tracking) {
    std::vector<Token> tokens = tokenize("let x = 42");
    
    CLAW_ASSERT(tokens.size() >= 5);
    
    // First token (let) should be at line 1, col 1
    CLAW_ASSERT(tokens[0].span.start.line == 1);
    CLAW_ASSERT(tokens[0].span.start.column == 1);
    
    // x should be at line 1, col 5
    CLAW_ASSERT(tokens[1].span.start.line == 1);
    CLAW_ASSERT(tokens[1].span.start.column == 5);
    
    // Number should be at line 1, col 9
    CLAW_ASSERT(tokens[3].span.start.line == 1);
    CLAW_ASSERT(tokens[3].span.start.column == 9);
    
    return TestStatus::Pass;
}

CLAW_TEST(multi_line_location) {
    std::vector<Token> tokens = tokenize("let x = 1\nlet y = 2");
    
    // First let at line 1
    CLAW_ASSERT(tokens[0].span.start.line == 1);
    // Second let at line 2
    CLAW_ASSERT(tokens[3].span.start.line == 2);
    
    return TestStatus::Pass;
}

CLAW_TEST(boolean_literals) {
    std::vector<Token> tokens = tokenize("true false");
    
    CLAW_ASSERT(tokens.size() >= 3);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Kw_true);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Kw_false);
    
    return TestStatus::Pass;
}

CLAW_TEST(nothing_keyword) {
    std::vector<Token> tokens = tokenize("null");
    
    CLAW_ASSERT(tokens.size() >= 2);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Kw_null);
    
    return TestStatus::Pass;
}
