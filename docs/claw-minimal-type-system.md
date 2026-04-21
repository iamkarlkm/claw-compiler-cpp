# Claw Minimal Type System

## Core Philosophy: Simple Syntax, Powerful Semantics

### Only 4 Base Types
- `byte` - 8-bit unsigned (0-255)
- `int` - machine word signed integer  
- `float` - machine word floating point
- `name` - named binary stream (the fundamental abstraction)

Everything else is derived from these!

### No Complex Type Declarations
Instead of complex syntax like:
```rust
let x: Vec<HashMap<String, Option<u32>>> = Vec::new();
```

Claw uses intuitive binary stream operations:
```claw
name config = byte[1024]        // Just a binary stream
name users = config[1..100]     // Sub-stream for user data
name user_count = config.u32[1] // First 4 bytes as count
```

## Type Inference Through Operations

### Automatic Type Promotion
```claw
name buffer = byte[16]
buffer.u32[1] = 42              // Automatically treats bytes 1-4 as u32
buffer.float[1] = 3.14          // Same bytes, different interpretation
```

### Context-Based Typing
The operation determines the type, not verbose declarations:
```claw
// Instead of: let file: File = File::open("data.bin")?
name data = file("data.bin")    // "file" operation implies file stream type

// Instead of: let socket: TcpStream = TcpStream::connect("1.2.3.4:80")?
name conn = tcp("1.2.3.4:80")   // "tcp" operation implies network stream
```

## Unified Collection Operations

### No Separate Array/List/Vector Types
All collections are just named binary streams with length:
```claw
name numbers = int[10]          // 10 integers (40 bytes on 32-bit)
numbers[1] = 42                 // First element
numbers[10] = 100               // Tenth element

name text = byte[100]           // String as byte stream
text[1] = 'H'                   // First character
```

### Slicing Creates New Names
```claw
name first_five = numbers[1..5] // New name for subset
name rest = numbers[6..]        // From 6th to end
```

## Function Types Through Usage

### No Explicit Function Signatures Needed
```claw
// Traditional: fn add(x: int, y: int) -> int { x + y }
fn add(x, y) { x + y }          // Types inferred from usage

// Works with any numeric types in the binary stream
name result = add(numbers[1], numbers[2])
```

### Higher-Order Functions Naturally
```claw
fn map(stream, func) {
    name result = byte[stream.len()]
    for i = 1 to stream.len() {
        result[i] = func(stream[i])
    }
    return result
}

name doubled = map(numbers, fn(x) { x * 2 })
```

## Error Handling Through Optional Values

### No Complex Result<T, E> Types
```claw
// Instead of: let file = File::open("missing.txt").unwrap_or_default()
name file = file("missing.txt") or byte[0]  // Empty stream if failed

// Conditional operations
if file.len() > 0 {
    // Process the file
}
```

## Memory Management Through Naming Scope

### No Explicit Ownership/Lifetime Annotations
```claw
fn process_data() {
    name temp = byte[1024]      // Created in scope
    // Use temp...
}                               // Automatically cleaned up when name goes out of scope
```

### Shared Access Through Name Passing
```claw
fn reader(shared_data) {        // Name passed by reference
    return shared_data[1]       // Safe shared read access
}

fn writer(shared_data, value) {
    shared_data[1] = value      // Safe shared write access  
}
```

## Hardware Integration Without Special Types

### Everything is Just Binary Streams
```claw
// GPIO control - no special register types needed
name gpio = byte[0x40000000, 256]
gpio.u32[1] = 0xFF              // Set direction register
gpio.u32[2] = 0xAA              // Set data register

// Memory-mapped I/O
name framebuffer = byte[0x80000000, 1920*1080*4]
framebuffer[1] = 0xFF0000FF     // Set first pixel (RGBA)
```

## The Beauty of Simplicity

With just 4 base types and the `name` abstraction:
- No complex generic syntax
- No verbose type annotations  
- No separate collection types
- No explicit memory management
- No special hardware access APIs

Everything becomes intuitive binary stream operations with 1-based indexing that matches human thinking.

The compiler handles all the complexity behind the scenes, while the programmer works with simple, consistent abstractions.