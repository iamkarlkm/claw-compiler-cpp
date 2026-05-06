# Project Agent Contract

<!-- brain:begin agents-contract -->
Use this file as a Brain-managed project context entrypoint for `claw-compiler`.

Read the linked context files before substantial work. Prefer the `brain` skill and `brain` CLI for project memory, retrieval, and durable context updates.

## Table Of Contents

- [Overview](./.brain/context/overview.md)
- [Architecture](./.brain/context/architecture.md)
- [Standards](./.brain/context/standards.md)
- [Workflows](./.brain/context/workflows.md)
- [Memory Policy](./.brain/context/memory-policy.md)
- [Current State](./.brain/context/current-state.md)
- [Policy](./.brain/policy.yaml)

## Human Docs

- [README.md](./README.md)
- [claw-comment-syntax.md](./docs/claw-comment-syntax.md)
- [claw-compiler-design.md](./docs/claw-compiler-design.md)
- [claw-event-system.md](./docs/claw-event-system.md)
- [claw-language-spec.md](./docs/claw-language-spec.md)
- [claw-memory-model.md](./docs/claw-memory-model.md)
- [claw-minimal-type-system.md](./docs/claw-minimal-type-system.md)
- [claw-ml-compiler-integration.md](./docs/claw-ml-compiler-integration.md)
- [claw-naming-model.md](./docs/claw-naming-model.md)
- [claw-runtime-architecture.md](./docs/claw-runtime-architecture.md)
- [claw-syntax-update.md](./docs/claw-syntax-update.md)
- [claw-tensor-optimization.md](./docs/claw-tensor-optimization.md)
- [claw-tensor-quickstart.md](./docs/claw-tensor-quickstart.md)
- [claw-type-system.md](./docs/claw-type-system.md)
- [project-architecture.md](./docs/project-architecture.md)
- [project-overview.md](./docs/project-overview.md)
- [project-workflows.md](./docs/project-workflows.md)
- [self-modification-plan.md](./docs/self-modification-plan.md)

## Required Workflow

1. If no validated session is active, run `brain prep --task "<task>"`.
2. If a session is already active, run `brain prep`.
3. Read this file and the linked context files still needed for the task.
4. Use `brain context compile --task "<task>"` only when you need the lower-level packet compiler directly.
5. Retrieve project memory with `brain find claw-compiler` or `brain search "claw-compiler <task>"` when the compiled packet is not enough.
6. Use `brain edit` for durable context updates to AGENTS.md, docs, or .brain notes.
7. Use `brain session run -- <command>` for required verification commands.
8. Finish with `brain session finish` so policy checks can enforce verification and surface promotion review when durable follow-through is still needed.
<!-- brain:end agents-contract -->

## Local Notes

Add repo-specific notes here. `brain context refresh` preserves content outside managed blocks.
