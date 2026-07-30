# Codex Workflow

## Purpose

Use subagents to reduce repeated context while preserving independent testing
and review.

## Accepted-plan workflow

When `docs/plans/haptics-nix-refactor.md` is accepted:

1. Do not spawn `planner`.
2. Primary thread reads only the current step and latest handoff.
3. Spawn up to two `investigator` agents in parallel for independent,
   nonoverlapping questions.
4. Close or summarize investigators.
5. Spawn one `implementer` or `complex_implementer`.
6. Spawn `tester`.
7. Spawn `reviewer`.
8. Route accepted findings back to one implementer.
9. Rerun affected tests.
10. Run `scripts/dev/graphify-checkpoint`.
11. Record a concise step result in the plan and continue.

## Concurrency

Allowed:

```text
orchestrator
├── investigator A (read-only)
└── investigator B (read-only)
```

or:

```text
orchestrator
└── one editing implementer
```

Do not run an editor concurrently with another agent whose task depends on the
same changing files.

## Handoffs

Each worker receives only:

- Exact step and acceptance criteria.
- Relevant scoped `AGENTS.md`.
- Relevant Graphify result.
- Files it may edit or inspect.
- Current focused diff.
- Previous worker's concise result.

Do not resend the entire plan or repository history.

## Output limits

- Investigator: 800 words.
- Implementer: 800 words.
- Tester: 500 words.
- Reviewer: 800 words.
- Include raw logs only around the first useful failure.

## Escalation

Use `complex_implementer` for:

- NixOS module/service integration.
- IPC framing and capability negotiation.
- Audio queues and mixer.
- Legacy-rumble synthesis.
- Haptics state arbitration.
- Concurrency and reconnect cleanup.

Use Sol only if the repository proves an architectural contradiction not
resolved by the accepted plan. Report the contradiction before replanning.

## Step closure

A step closes only after implementation, focused testing, independent review,
accepted fixes, rerun checks, documentation update, and Graphify checkpoint.
