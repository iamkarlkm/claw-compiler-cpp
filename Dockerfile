# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    clang \
    llvm \
    lld \
    libreadline-dev \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

ENV CLAW_ENABLE_WEBTRANSPORT=0
RUN make all CXX=clang++

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    libreadline8 \
    llvm \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/claw /usr/local/bin/claw
COPY --from=builder /build/claw-lsp /usr/local/bin/claw-lsp
COPY --from=builder /build/claw-repl /usr/local/bin/claw-repl
COPY --from=builder /build/claw-debugger /usr/local/bin/claw-debugger

WORKDIR /src
ENTRYPOINT ["claw"]
CMD ["--help"]
