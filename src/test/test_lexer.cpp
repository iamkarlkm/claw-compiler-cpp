// Claw Compiler - Lexer Unit Tests

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
    } while (token.type != TokenType::End && token.type != TokenType::Error);
    return tokens;
}

CLAW_TEST_SUITE(Lexer);

CLAW_TEST(keywords_recognized) {
    std::vector<Token> tokens = tokenize("fn let if else match for while loop return break continue serial process");
    
    CLAW_ASSERT(tokens.size() >= 12);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Fn);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Let);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::If);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::Else);
    CLAW_ASSERT_EQ(tokens[4].type, TokenType::Match);
    CLAW_ASSERT_EQ(tokens[5].type, TokenType::For);
    CLAW_ASSERT_EQ(tokens[6].type, TokenType::While);
    CLAW_ASSERT_EQ(tokens[7].type, TokenType::Loop);
    CLAW_ASSERT_EQ(tokens[8].type, TokenType::Return);
    CLAW_ASSERT_EQ(tokens[9].type, TokenType::Break);
    CLAW_ASSERT_EQ(tokens[10].type, TokenType::Continue);
    CLAW_ASSERT_EQ(tokens[11].type, TokenType::Serial);
    
    return TestStatus::Pass;
}

CLAW_TEST(integers_parsed) {
    std::vector<Token> tokens = tokenize("42 0 123456 0xFF 0b101 0o77");
    
    CLAW_ASSERT(tokens.size() >= 7);  // 6 numbers + End
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Number);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Number);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Number);
    
    // Check hex
    CLAW_ASSERT(tokens[3].type == TokenType::Number);
    // Check binary
    CLAW_ASSERT(tokens[4].type == TokenType::Number);
    // Check octal
    CLAW_ASSERT(tokens[5].type == TokenType::Number);
    
    return TestStatus::Pass;
}

CLAW_TEST(floats_parsed) {
    std::vector<Token> tokens = tokenize("3.14 0.5 .5 3. 1e10 1.5e-5");
    
    CLAW_ASSERT(tokens.size() >= 7);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Number);
    
    auto& val0 = std::get<double>(tokens[0].value);
    CLAW_ASSERT_EQ(val0, 3.14);
    
    return TestStatus::Pass;
}

CLAW_TEST(strings_parsed) {
    std::vector<Token> tokens = tokenize("\"hello\" \"world\" \"multi\\nline\"");
    
    CLAW_ASSERT(tokens.size() >= 4);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::String);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::String);
    
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
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Plus);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Minus);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Star);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::Slash);
    CLAW_ASSERT_EQ(tokens[4].type, TokenType::Percent);
    CLAW_ASSERT_EQ(tokens[5].type, TokenType::Equal);
    CLAW_ASSERT_EQ(tokens[6].type, TokenType::EqualEqual);
    CLAW_ASSERT_EQ(tokens[7].type, TokenType::BangEqual);
    CLAW_ASSERT_EQ(tokens[8].type, TokenType::Less);
    CLAW_ASSERT_EQ(tokens[9].type, TokenType::Greater);
    CLAW_ASSERT_EQ(tokens[10].type, TokenType::LessEqual);
    CLAW_ASSERT_EQ(tokens[11].type, TokenType::GreaterEqual);
    CLAW_ASSERT_EQ(tokens[12].type, TokenType::AmpersandAmpersand);
    CLAW_ASSERT_EQ(tokens[13].type, TokenType::PipePipe);
    
    return TestStatus::Pass;
}

CLAW_TEST(delimiters_recognized) {
    std::vector<Token> tokens = tokenize("( ) { } [ ] , . ; : -> =>");
    
    CLAW_ASSERT(tokens.size() >= 12);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::LeftParen);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::RightParen);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::LeftBrace);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::RightBrace);
    CLAW_ASSERT_EQ(tokens[4].type, TokenType::LeftBracket);
    CLAW_ASSERT_EQ(tokens[5].type, TokenType::RightBracket);
    
    return TestStatus::Pass;
}

CLAW_TEST(single_line_comment) {
    std::vector<Token> tokens = tokenize("let x // this is a comment\nlet y");
    
    // Should parse: let, identifier, let, identifier, End
    CLAW_ASSERT(tokens.size() >= 5);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Let);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    // Comment should be skipped
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Let);
    CLAW_ASSERT_EQ(tokens[3].type, TokenType::Identifier);
    
    return TestStatus::Pass;
}

CLAW_TEST(multi_line_comment) {
    std::vector<Token> tokens = tokenize("let x /* multi\nline\ncomment */ let y");
    
    CLAW_ASSERT(tokens.size() >= 5);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Let);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    CLAW_ASSERT_EQ(tokens[2].type, TokenType::Let);
    
    return TestStatus::Pass;
}

CLAW_TEST(nested_comments) {
    std::vector<Token> tokens = tokenize("let /* outer /* inner */ outer */ x");
    
    CLAW_ASSERT(tokens.size() >= 3);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Let);
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
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::True);
    CLAW_ASSERT_EQ(tokens[1].type, TokenType::False);
    
    return TestStatus::Pass;
}

CLAW_TEST(nothing_keyword) {
    std::vector<Token> tokens = tokenize("nothing");
    
    CLAW_ASSERT(tokens.size() >= 2);
    CLAW_ASSERT_EQ(tokens[0].type, TokenType::Nothing);
    
    return TestStatus::Pass;
}
