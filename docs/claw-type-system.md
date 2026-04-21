# Claw Type System - Unified Binary Stream Types

## Core Principles
1. **Everything is a named binary stream** - memory, files, hardware, network
2. **1-based indexing everywhere** - intuitive human-friendly addressing  
3. **Zero-cost type reinterpretation** - compile-time only, no runtime overhead
4. **Unified I/O model** - same operations work on all stream types

## Base Types
- `byte` - 8-bit unsigned integer (0-255)
- `u8/u16/u32/u64` - unsigned integers of various sizes
- `i8/i16/i32/i64` - signed integers of various sizes  
- `f32/f64` - floating point numbers
- `bool` - boolean (stored as byte, true=1, false=0)

## Stream Declaration Syntax
```claw
// Static allocation
name buffer = byte[1024]           // 1024 bytes, indices 1..1024
name numbers = u32[100]            // 100 u32 values, indices 1..100

// Dynamic allocation  
name dynamic = byte[]              // Empty initially
dynamic.resize(50)                 // Now 50 bytes, indices 1..50

// Memory-mapped hardware
name gpio = byte[0x40000000, 256]  // Hardware registers at address

// File streams
name config = file("/etc/app.conf") // File as binary stream
name log = file("/var/log/app.log", append=true)

// Network streams
name server = tcp("0.0.0.0:8080", listen=true)
name client = tcp("192.168.1.100:8080")
```

## Type Reinterpretation
```claw
name data = byte[16]

// Reinterpret as different types (1-based indexing)
name magic = data.u32[1]           // Bytes 1-4 as u32
name size = data.u32[2]            // Bytes 5-8 as u32  
name flags = data.u16[5]           // Bytes 9-10 as u16
name checksum = data.u16[6]        // Bytes 11-12 as u16

// Nested reinterpretation
name header = data[1..8]           // Sub-stream bytes 1-8
name header_magic = header.u32[1]  // Header bytes 1-4 as u32
```

## Range Operations
```claw
name buffer = byte[100]

// Inclusive ranges (both ends included)
name first_10 = buffer[1..10]      // Bytes 1-10
name last_5 = buffer[96..100]      // Bytes 96-100

// Open-ended ranges  
name from_10 = buffer[10..]        // Bytes 10-100
name to_50 = buffer[..50]          // Bytes 1-50

// Assignment with ranges
buffer[1..10] = source[1..10]      // Copy first 10 bytes
```

## Compiler Transformations
All 1-based operations are transformed to 0-based at compile time:

Source code: `data.u32[1]`
→ Compiled to: `*((u32*)(data + 0))`

Source code: `buffer[5..10]`  
→ Compiled to: `&buffer[4], length=6`

This ensures zero runtime overhead while maintaining human-friendly syntax.

## Safety Guarantees
- **Bounds checking**: Compile-time verification of range operations
- **Alignment checking**: Ensure proper alignment for multi-byte types
- **Lifetime tracking**: Prevent use-after-free and dangling references
- **Type safety**: Prevent invalid reinterpretations

## Error Handling
```claw
// Safe operations return Result types
match safe_read(data, offset, length) {
    Ok(slice) => process(slice),
    Err(e) => handle_error(e),
}

// Unsafe operations for performance-critical code
unsafe {
    let raw_ptr = data.as_ptr();
    // Direct pointer manipulation
}
```

## Integration with Existing Systems
- **C compatibility**: Can pass Claw streams to C functions as void*
- **POSIX compliance**: File operations map directly to system calls  
- **Hardware compatibility**: Memory-mapped I/O works with existing drivers
- **Network stack**: TCP/UDP operations use standard socket APIs