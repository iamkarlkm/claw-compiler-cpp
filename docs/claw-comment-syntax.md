# Claw Comment Syntax

## Single-line Comments
Use `//` for single-line comments:
```claw
name x = byte[16];  // This is a single-line comment
```

## Multi-line Comments  
Use `/* */` for multi-line comments:
```claw
/*
This is a multi-line comment
that can span multiple lines
and is useful for documentation
*/
name y = u32[4];
```

## Nested Comments
Multi-line comments can be nested (unlike C/C++):
```claw
/*
Outer comment
    /* Inner comment */
Still in outer comment
*/
```

## Documentation Comments
Use `///` for documentation comments that can be extracted by clawdoc:
```claw
/// Adds two numbers together
/// @param a - first number  
/// @param b - second number
/// @returns sum of a and b
serial process add(a: u32, b: u32) {
    result.u32[1] = a + b;
}
```

## Comment Placement
Comments can appear anywhere whitespace is allowed:
- Before statements
- After statements  
- Between expressions
- Inside function bodies
- Around type declarations

## Compiler Behavior
- All comments are completely ignored by the compiler
- No runtime overhead from comments
- Documentation comments are processed by clawdoc tool only