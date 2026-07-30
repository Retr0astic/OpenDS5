# Codex Run Prompt

Treat `docs/plans/haptics-nix-refactor.md` as the accepted implementation plan.

Do not invoke the planner. Start at Step 0 and execute one numbered step at a
time. Follow the root and scoped `AGENTS.md` files.

Use the named project agents and the repository limits:

- `max_threads = 3`
- `max_depth = 1`
- At most two concurrent read-only investigators
- Only one editing implementer at a time
- No nested delegation
- Luna low by default
- `complex_implementer` only for the high-impact tasks identified in the plan
- `tester` after implementation
- `reviewer` after every material phase
- `final_reviewer` only in Step 10

Keep the primary thread orchestration-only. Give each worker only the current
step, relevant scoped instructions, focused Graphify result, current diff, and
previous concise handoff.

At the end of every numbered step run:

`scripts/dev/graphify-checkpoint`

Preserve unrelated worktree changes. Do not reset, clean, commit, push, rebase,
force-update, install host-global tools, or alter persistent host configuration.

Stop only for:

1. An overlapping dirty-worktree conflict.
2. An action requiring explicit permission.
3. A concrete architectural contradiction that cannot be resolved from the
   accepted plan.
4. Evidence that the configured child model/effort is not being applied and
   continuing would risk unexpected quota consumption.

Report exact evidence rather than broadly replanning.
