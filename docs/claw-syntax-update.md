# Claw Syntax Update: Statement Termination with Semicolon

## Change Summary
- All statements must end with semicolon `;`
- Improves parsing clarity and reduces ambiguity
- Consistent with most programming languages while maintaining Claw's simplicity

## Updated Examples

### Variable Declarations
```claw
name data = byte[1024];
name num = data.u32[1];
name text = data[5..100];
```

### Function Definitions
```claw
fn process(data: name byte[]) -> name byte[] {
    name result = byte[data.len()];
    
    for i in 1..data.len() {
        result[i] = data[i] + 1;
    }
    
    return result;
}
```

### Process Definitions
```claw
serial process file_modified(path: string) {
    // Process body
};

parallel process system_startup() {
    // Process body  
};
```

### Subscriptions
```claw
subscribe file_modified {
    fn handler1(path: string) -> bool {
        if path.ends_with(".log") {
            compress_old_logs();
            return true;
        }
        return false;
    }
    
    fn handler2(path: string) -> bool {
        if path.ends_with(".conf") {
            reload_configuration();
            return true;
        }
        return false;
    }
};
```

### Main Function
```claw
fn main() {
    name x = u32[1];
    name y = u32[1];
    
    x[1] = 10;
    y[1] = 5;
    
    publish add(x[1], y[1]);
    println("Result: {}", result.u32[1]);
};
```

## Compiler Implementation Notes
- Lexer will tokenize semicolons as statement terminators
- Parser will enforce semicolon usage for all statements
- Missing semicolons will produce clear error messages
- This change simplifies the grammar and makes parsing more robust