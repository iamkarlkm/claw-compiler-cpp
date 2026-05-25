# Claw Compiler - Multi-stage Docker build
# Supports: Linux (bytecode, interpreter, C codegen modes)
# Note: AOT native codegen targets macOS Mach-O; use --mode=bytecode on Linux.

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    clang \
    llvm \
    llvm-dev \
    libreadline-dev \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Disable WebTransport (libmsquic not available in Ubuntu repos)
RUN make all CLAW_ENABLE_WEBTRANSPORT=0 CXX=clang++

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    libreadline8 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/claw /usr/local/bin/
COPY --from=builder /build/claw-lsp /usr/local/bin/
COPY --from=builder /build/claw-repl /usr/local/bin/

ENTRYPOINT ["claw"]
CMD ["--help"]
