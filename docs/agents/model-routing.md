# Model Routing

## Defaults

| Role | Model | Effort | Use |
|---|---|---:|---|
| Investigator | `gpt-5.6-luna` | low | Call paths, file ownership, narrow evidence |
| Implementer | `gpt-5.6-luna` | low | Mechanical and decision-complete changes |
| Complex implementer | `gpt-5.6-luna` | medium | IPC, NixOS, mixer, synthesis, state |
| Tester | `gpt-5.6-terra` | low | Prescribed deterministic checks |
| Reviewer | `gpt-5.6-terra` | medium | Focused independent review |
| Final reviewer | `gpt-5.6-terra` | high | Cross-subsystem integration review |
| Planner | `gpt-5.6-sol` | medium | Only when no accepted plan exists |

## Luna-low tasks

- Graphify queries and repository mapping.
- Documentation.
- `AGENTS.md` compaction.
- Package metadata and file-output cleanup.
- Desktop entries and icons.
- Mechanical derivation separation after boundaries are decided.
- Test fixture additions with exact expected behavior.

## Luna-medium tasks

- NixOS service and module ownership.
- Capability negotiation.
- Unix-socket stream implementation.
- Audio queue design.
- Source-aware mixing and limiting.
- Legacy-rumble synthesis.
- State arbitration and lease removal.
- Reconnect and crash cleanup.

## Runtime verification

A custom-agent TOML expresses intended routing, not proof of actual runtime
selection. Before launching many workers:

1. Parse `.codex/config.toml` and `.codex/agents/*.toml`.
2. Spawn one trivial investigator task.
3. Inspect whatever session metadata or agent UI the installed Codex version
   exposes.
4. Confirm the effective role/model/effort when possible.
5. If model routing cannot be verified, do not claim quota savings; reduce
   concurrency and inspect the installed Codex version/configuration first.
