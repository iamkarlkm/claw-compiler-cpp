#compdef claw

local curcontext="$curcontext" state line
local -a opts

opts=(
    '(-t --tokens)'{-t,--tokens}'[Print tokens only]'
    '(-a --ast)'{-a,--ast}'[Print AST]'
    '--compact-ast[Print compact AST representation]'
    '(-s --semantic)'{-s,--semantic}'[Run semantic analysis]'
    '(-T --typecheck)'{-T,--typecheck}'[Run type checking]'
    '(-r --run)'{-r,--run}'[Interpret AST directly]'
    '(-b --bytecode)'{-b,--bytecode}'[Compile to bytecode and run in VM]'
    '(-j --jit)'{-j,--jit}'[JIT compile and execute]'
    '(-H --hybrid)'{-H,--hybrid}'[Hybrid mode: interpret + JIT hot paths]'
    '(-C --ccodegen)'{-C,--ccodegen}'[Generate C code]'
    '(-n --native)'{-n,--native}'[Generate x86-64 native code]'
    '--aot[AOT compile to executable]'
    '(-w --wasm)'{-w,--wasm}'[Generate WebAssembly]'
    '(-i --repl)'{-i,--repl}'[Start REPL interactive mode]'
    '(-o --output)'{-o,--output}'[Output file]:output file:_files'
    '-O[Optimization level]:level:(0 1 2 3)'
    '(-v --verbose)'{-v,--verbose}'[Verbose output]'
    '--time[Show compilation time]'
    '--show-ir[Show generated IR/code]'
    '--diagnostics-json[Output diagnostics as JSON]'
    '(-h --help)'{-h,--help}'[Show help]'
)

_arguments -C -s \
    "$opts[@]" \
    '*:claw file:_files -g "*.claw"' \
    && return 0

return 1
