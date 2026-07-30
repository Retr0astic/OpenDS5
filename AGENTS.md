# OpenDS5 Repository Instructions

## Scope

These instructions apply to the entire repository. More specific instructions
exist in:

- `nix/AGENTS.md`
- `vds/AGENTS.md`
- `ds5-bridge/companion/AGENTS.md`

Supporting workflow documents are under `docs/agents/`.

## Repository map

- `ds5-bridge/companion/`: Electron GUI, bridge service, Linux audio helper.
- `vds/`: userspace daemon, protocol, control utility, virtual-device stack.
- `nix/`: Nix packages and NixOS module.
- `docs/architecture/`: authoritative current architecture.
- `docs/protocol/`: versioned protocol contracts.
- `docs/testing/`: automated and hardware validation matrices.
- `docs/plans/`: accepted execution plans.
- `scripts/dev/`: deterministic developer utilities.

## Plan authority

When the user supplies or accepts a decision-complete plan, treat it as the
implementation authority and do not invoke the planner. Investigate only enough
to verify repository facts and identify concrete contradictions.

Do not replace an accepted architecture with a new one without reporting the
specific incompatibility and obtaining direction.

## Orchestration

The primary thread coordinates material work; delegate bounded tasks to named
subagents.

Default sequence:

1. Investigator maps the relevant path and reports evidence.
2. Implementer makes the smallest coherent change.
3. Tester runs the prescribed checks and reports exact failures.
4. Reviewer independently reviews the focused diff.
5. Implementer fixes accepted findings.
6. Tester reruns affected checks.

Limits:

- Maximum two concurrent read-only workers.
- Maximum one editing worker at a time.
- No nested subagent delegation.
- Do not give multiple implementers overlapping files.
- Reuse an agent only while its context remains narrow and relevant.

See `docs/agents/workflow.md` and `docs/agents/role-contracts.md`.

## Model routing

Use the cheapest capable role:

- `investigator`: Luna low.
- `implementer`: Luna low.
- `complex_implementer`: Luna medium.
- `tester`: Terra low.
- `reviewer`: Terra medium.
- `final_reviewer`: Terra high.
- `planner`: Sol medium, only when no accepted plan exists.

Use `complex_implementer` for concurrency, IPC, real-time audio, state-machine,
NixOS module, kernel-integration, and protocol-compatibility work. Mechanical
edits, documentation, file moves, metadata, and bounded tests use Luna low.

Never assume the declared child model was actually selected. Step 0 of an
accepted plan should verify the configuration and inspect available runtime
metadata before high-volume delegation.

## Graph-first navigation

Before broad source reading:

1. Ask Graphify a specific architecture or call-path question.
2. Read only the returned files and necessary surrounding ranges.
3. Use targeted text search when Graphify is incomplete.
4. Avoid whole-repository dumps and repeated architecture summaries.

At the end of every numbered plan step run:

```sh
scripts/dev/graphify-checkpoint
```

Do not run model-backed clustering during routine implementation.

## Worktree and Git safety

- Inspect `git status --short` before editing.
- Preserve unrelated user changes.
- Do not overwrite a dirty file without first understanding the overlap.
- Do not run `git reset`, `git clean`, destructive checkout, rebase, force push,
  or history rewriting.
- Do not commit, push, create tags, or open pull requests without explicit user
  permission.
- Do not update unrelated dependencies or `flake.lock`.
- Keep generated build output outside tracked source paths where practical.

## Change discipline

- Make the minimum coherent change required by the current step.
- Follow existing naming, formatting, and error-handling conventions.
- Do not perform opportunistic refactors.
- Version protocol changes and preserve explicit compatibility behavior.
- Add tests for regressions before removing the mechanism that caused them.
- Update authoritative documentation in the same phase as behavior changes.
- Never claim hardware behavior from source inspection or unit tests alone.

## Required validation

Use the existing Nix development shell:

```sh
nix develop
```

Run only the checks relevant to the changed subsystem, plus `git diff --check`.
Canonical commands and escalation rules are in `docs/agents/validation.md`.

For material changes, report:

- Files changed.
- Tests and builds run.
- Exact failures or skipped checks.
- Hardware behavior not validated.
- Remaining reviewer findings.

## Review requirement

Every material code or system-integration phase requires independent review.
The reviewer is read-only and must not implement fixes. Findings must be
specific, prioritized, and tied to changed behavior.

The final review must examine the complete Nix-to-GUI-to-daemon-to-controller
contract, not only individual files.

See `docs/agents/review-checklist.md`.

## Permission boundaries

Normal application execution must not make persistent privileged changes.
Installation, service registration, kernel-module loading, udev rules,
WirePlumber integration, user groups, and Bluetooth policy belong to explicit
installers or declarative NixOS configuration.

Stop for explicit permission before:

- Committing or pushing.
- Installing or upgrading host-global tools.
- Changing persistent host configuration outside the repository.
- Destructive Git or filesystem operations.
- Enabling globally disruptive Bluetooth behavior.

## Completion

A step is complete only when:

- Its acceptance criteria are met.
- Relevant tests pass or failures are documented.
- Independent review has no unresolved blocking finding.
- Documentation reflects implemented behavior.
- `scripts/dev/graphify-checkpoint` succeeds.
- The worktree retains unrelated user changes.
