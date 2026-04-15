# Claw Unified Naming Model

## Core Philosophy: Everything is a Named Binary Stream

### Fundamental Principle
- Every entity in the system is a **named binary stream** (`name`)
- Names are first-class citizens with rich metadata and capabilities  
- All operations are performed through name-based interfaces
- Consistent 1-based indexing across all contexts

### Name Declaration Syntax
```claw
// Basic binary stream naming
name x = byte[16]           // 16-byte binary stream named 'x'
name config = file("/etc/app.conf")  // File as named stream
name sensor = hardware("gpio:13")    // Hardware register as stream
name network = socket("tcp://localhost:8080") // Network connection as stream

// Type-based range naming with 1-based indexing
name y = x.long[1]          // First 8-byte long from x (bytes 1-8)
name z = x.long[2]          // Second 8-byte long from x (bytes 9-16)
name header = packet.byte[1..4]     // First 4 bytes of packet
name payload = packet.byte[5..end]  // Remaining bytes
```

### 1-Based Indexing Implementation
- **User-facing**: All arrays, streams, and collections use 1-based indexing
- **Compiler transformation**: Automatically converts to 0-based for underlying operations
- **Error prevention**: Eliminates off-by-one errors in human reasoning
- **Consistency**: Same indexing rules apply to files, memory, arrays, strings, etc.

### Compiler-Level Index Translation
```rust
// User writes (1-based):
name value = buffer.int[5]

// Compiler generates (0-based internally):
let offset = (5 - 1) * size_of::<i32>();
let value = read_int_at(buffer, offset);
```

### Unified Operations on Named Streams
```claw
// Read operations
let first_byte = x.byte[1].read()
let second_long = x.long[2].read()

// Write operations  
x.byte[1].write(0xFF)
x.long[2].write(123456789)

// Transform operations
name encrypted = x.encrypt(aes_key)
name compressed = x.compress(gzip)

// Pipeline operations
name result = x
    .filter(|b| b > 0x80)
    .map(|b| b.rotate_left(4))
    .reduce(|a, b| a ^ b)
```

### Hierarchical Naming
```claw
// Nested naming for complex structures
name packet = byte[1024]
name packet.header = packet.byte[1..64]
name packet.header.magic = packet.header.byte[1..4]  
name packet.header.length = packet.header.long[2]
name packet.payload = packet.byte[65..packet.header.length.read()]

// Hardware example
name device = hardware("i2c:0x48")
name device.temperature = device.byte[1..2]
name device.config = device.byte[3..4]
```

### Type System Integration
- **Strong typing**: Each name has a compile-time type
- **Type inference**: Automatic type deduction from context
- **Generic names**: Template-based naming for reusable patterns
- **Zero-cost abstractions**: No runtime overhead for naming operations

### Memory Safety Guarantees
- **Bounds checking**: Compile-time verification of index ranges
- **Lifetime tracking**: Automatic cleanup when names go out of scope  
- **Aliasing control**: Prevent data races through ownership semantics
- **Immutable by default**: Names are read-only unless explicitly mutable

### Interoperability
- **C compatibility**: Seamless integration with existing C code
- **File system integration**: Direct mapping to Linux file operations
- **Hardware abstraction**: Uniform interface for different hardware platforms
- **Network transparency**: Same operations work on local and remote streams