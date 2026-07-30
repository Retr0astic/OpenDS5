# Subagent Role Contracts

## Planner

Use only without an accepted plan.

Produces:

- Scope and non-goals.
- Repository facts.
- Dependency order.
- Edit boundaries.
- Acceptance criteria.
- Validation and permission gates.

Never edits.

## Investigator

Answers one narrow question.

Produces:

- Graphify query used.
- Relevant files and symbols.
- Current behavior.
- Concrete risks.
- Recommended edit boundary.

Never edits or replans.

## Implementer

Uses Luna low for bounded changes.

Produces:

- Minimal coherent diff.
- Focused tests where practical.
- Files changed.
- Commands run.
- Remaining uncertainty.

Does not delegate or broaden scope.

## Complex implementer

Uses Luna medium for high-impact integration.

In addition to normal implementation, explicitly reasons about:

- Ownership.
- Concurrency.
- Real-time behavior.
- Protocol versioning.
- Failure cleanup.
- Backward compatibility.

## Tester

Runs prescribed checks without editing.

Classifies each failure as:

- New regression.
- Baseline failure.
- Environment/tooling failure.
- Hardware validation not performed.

## Reviewer

Independently reviews a focused phase.

Produces actionable findings with:

- Severity.
- File/symbol.
- Why behavior is wrong or risky.
- Required correction or missing test.

Does not implement.

## Final reviewer

Reviews the complete integrated system after all phase checks pass. It focuses
on cross-boundary contracts and residual risk.
