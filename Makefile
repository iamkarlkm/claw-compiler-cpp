# Claw Compiler - Simple Makefile
CXX = clang++
CXXFLAGS = -std=c++17 -O3 -Wall -I src
LDFLAGS = 

# 源文件列表 (header-only modules don't need .cpp files)
CLAW_SRCS = src/main.cpp \
            src/type/type_checker.cpp

# 目标可执行文件
TARGETS = claw claw-opt claw-tests

# 默认目标
all: $(TARGETS)

# 主编译器
claw: $(CLAW_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(CLAW_SRCS)

# 优化器
claw-opt: src/optimizer.cpp src/const_fold.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# 测试
claw-tests: src/test/run_tests.cpp src/test/test_lexer.cpp src/test/test_parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

# 清理
clean:
	rm -f $(TARGETS)

# 运行测试
test: claw-tests
	./claw-tests

# 演示
run: claw
	./claw -i claw-simple-syntax.claw

.PHONY: all clean test run
