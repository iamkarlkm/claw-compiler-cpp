# Claw Language Support for VS Code

Syntax highlighting, diagnostics, and LSP-powered IDE features for the Claw programming language.

## Features

- Syntax highlighting for `.claw` files
- Auto-completion via `claw-lsp`
- Hover information
- Go to definition
- Diagnostics (errors and warnings)
- Document formatting

## Requirements

- `claw-lsp` must be in your PATH or configured via settings

## Installation

```bash
cd editors/vscode
npm install
npm run compile
```

Then press `F5` in VS Code to launch the Extension Development Host.
