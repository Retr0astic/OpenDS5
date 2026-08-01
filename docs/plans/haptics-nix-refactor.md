# OpenDS5 Nix Packaging and Haptics Coexistence Plan

Status: accepted implementation plan.

## Objective

Implement:

1. Clean Nix package ownership and declarative NixOS integration with no Polkit
   during normal GUI launch.
2. Source-aware haptics coexistence where game haptics and OpenDS5 haptics mix by
   default and game haptics are suppressed only in explicit `replace` mode.

The existing repository Nix development flake is the development environment.

## Current execution state

The branch has already completed the following plan work:

- Steps 1–2: instruction/documentation scaffolding and separate Nix package
  ownership.
- Step 3: NixOS module ownership, including kernel-parameterized module wiring,
  udev, WirePlumber, `vdsd`, users, and explicit Bluetooth policy.
- Step 4: direct normal GUI launch and compatibility/service ownership changes;
  the privileged portable launcher remains separately named.
- Step 5: haptics observability and policy plumbing.
- Step 6: dedicated bounded OpenDS5 haptics IPC stream.
- Step 7: native-PCM source-aware mixer.
- Step 8: compatible-rumble synthesis and per-port state arbitration; the
  implementation and validation are recorded in
  `docs/superpowers/plans/2026-07-30-haptics-step8-handoff.md`.
- Step 9: removal of the global lease-era ownership semantics, recorded by
  commit `971f702` (`refactor: remove lease-era haptics ownership`).

Step 10, integrated validation, documentation, and final review, is complete;
the result and remaining non-blocking limitations are recorded below. The plan
descriptions remain the implementation authority, while this section records
the current branch position.

## Non-goals

- No unrelated UI redesign.
- No unrelated dependency upgrades.
- No rewriting the virtual-controller architecture.
- No claim of hardware success without manual testing.
- No automatic commits or pushes.
- No host-global Graphify installation.

## Agent and token rules

- This plan is accepted: do not spawn `planner`.
- Primary thread is orchestration-only for material work.
- Keep `max_threads = 3`, `max_depth = 1`.
- At most two read-only investigators concurrently.
- Only one editing implementer at a time.
- No nested agents.
- Luna low is the default.
- Use Luna medium only for NixOS integration, protocol, IPC, mixer, synthesis,
  state arbitration, and lifecycle concurrency.
- Terra low runs prescribed tests.
- Terra medium reviews each material phase.
- Terra high performs only the final integrated review.
- Give workers only the current step, relevant scoped instructions, Graphify
  result, focused diff, and preceding handoff.

At the end of every numbered step run:

```sh
scripts/dev/graphify-checkpoint
```

## Step 0 — Preflight, baseline, and routing verification

Agent use:

- Primary orchestrator.
- Up to two Luna-low investigators in parallel.
- No editor until dirty-worktree overlap is understood.

Actions:

1. Record branch and `git status --short`.
2. Enter `nix develop`.
3. Run `scripts/dev/validate-codex-config`.
4. Verify the installed Codex version can load named project agents.
5. Spawn one trivial `investigator` task and inspect available runtime metadata
   to confirm intended model/effort where possible.
6. Do not claim model-based quota savings when runtime selection is unverified.
7. Run baseline:
   - `nix flake check -L`
   - `nix build -L .#opends5 .#vds`
   - vDS CMake build/tests
   - companion typecheck/tests/build
   - `git diff --check`
8. Record failures as baseline, without fixing unrelated failures.
9. Investigators produce focused maps for:
   - Current Nix package/service/launcher ownership.
   - Current haptics PCM/HID/state/lease flow.

Acceptance:

- Dirty worktree is understood.
- Baseline failures are classified.
- Agent config parses.
- Actual model routing is verified or explicitly marked unverifiable.
- Exact phase edit scopes are known.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

## Step 1 — Instruction and documentation scaffolding

Agent use:

- Luna-low implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Install the compact root and scoped `AGENTS.md` structure.
2. Install complete `.codex/agents/*.toml` profiles.
3. Preserve `max_threads = 3`, `max_depth = 1`.
4. Add architecture, protocol, testing, and role documentation.
5. Mark `docs/PORTING.md` historical or point it to authoritative architecture
   docs; do not erase useful history.
6. Update references without changing product claims prematurely.

Acceptance:

- Instructions parse and scope correctly.
- Reviewer confirms no mandatory safety or OpenDS5 constraint was lost.
- No reviewer edits files; Luna fixes accepted findings.
- Root instructions remain compact.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

### Step 1 result / handoff

Instruction and documentation scaffolding was audited. `docs/PORTING.md` is
marked historical; README and installer references point to the accepted
target architecture documents while explicitly describing current behavior
until later steps land. Validation passed for the scoped documentation and
configuration checks (including TOML parsing, limits, links, and
`git diff --check`); the Graphify checkpoint passed inside `nix develop`.
Reviewer findings are cleared. Hardware validation is not applicable to this
documentation-only step.

## Step 2 — Separate Nix package ownership

Agent use:

- Luna-low investigator.
- Luna-low implementer for mechanical package separation.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Make `opends5` contain GUI-owned artifacts only.
2. Keep `vdsd`, `vdsctl`, udev, WirePlumber, and optional generic systemd unit
   in `vds`.
3. Keep kernel module in a kernel-parameterized derivation.
4. Remove daemon/system artifacts copied or symlinked into the GUI output.
5. Build the companion from its natural source directory or precise fileset.
6. Align Electron major and native module ABI.
7. Update package-output checks.

Acceptance:

- Output ownership matches `docs/architecture/nix-packaging.md`.
- GUI derivation builds.
- Daemon derivation builds.
- Native module and Electron smoke checks exist or have a documented blocker.
- `flake.lock` is unchanged unless separately justified.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

### Step 2 result / handoff

`opends5` now contains GUI-owned files only; `vds` remains the owner of
`vdsd`, `vdsctl`, udev, systemd, and WirePlumber assets, with the kernel
derivation still parameterized. The companion is built from a precise
repository fileset including `ds5-bridge/assets`; Electron 42, its lockfile,
and the captured npm dependency hash are aligned. The default flake app starts
the GUI directly, while the privileged portable launcher is explicit. Output
ownership, native-module, and Electron smoke checks were added. Full GUI,
aarch64, and hardware behavior remain untested; `flake.lock` is unchanged.
Required validation and Graphify checkpoint passed; reviewer findings cleared.

## Step 3 — Make NixOS module the sole system owner

Agent use:

- Luna-medium complex implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. NixOS module installs/composes GUI, vDS, and kernel package.
2. Build kernel module against `config.boot.kernelPackages.kernel`.
3. Register udev through `services.udev.packages`.
4. Register WirePlumber through
   `services.pipewire.wireplumber.configPackages`.
5. Define `vdsd` through `systemd.services`.
6. Define group and configured user membership.
7. Make Bluetooth exclusive-input behavior explicit, opt-in, and warned.
8. Add module evaluation and NixOS VM coverage where practical.

Acceptance:

- Module evaluates with default and nondefault kernel package sets.
- Service and socket permissions are deterministic.
- Normal configured boot does not need runtime installation.
- Global Bluetooth behavior cannot be enabled accidentally.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

## Step 4 — Remove Polkit from normal launch and add compatibility handshake

Agent use:

- Luna-low investigator.
- Luna-medium complex implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Default app and desktop entry launch the GUI directly.
2. Remove exact Nix-store-path daemon matching.
3. Add daemon version/capability reporting.
4. GUI checks required protocols/features and reports actionable failures.
5. Normal launch never calls `pkexec` or stops/replaces `vdsd`.
6. Retain a privileged portable launcher only if explicitly named and justified;
   it must not be default or presented as the NixOS path.
7. Update README and installer documentation to distinguish AppImage installation
   from declarative NixOS installation.

Acceptance:

- Normal launch has no Polkit path.
- Compatible daemon works regardless of store path.
- Incompatible daemon fails explicitly.
- No system service is manipulated by the GUI.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

## Step 5 — Add haptics observability and explicit policy plumbing

Agent use:

- Two Luna-low investigators may map daemon and companion paths in parallel.
- Luna-medium complex implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Define `off`, `mix`, and `replace` in the control protocol.
2. Pass persisted GUI policy through companion to daemon.
3. Add per-port status for:
   - game PCM;
   - game legacy motor state;
   - OpenDS5 PCM;
   - effective physical mode;
   - peaks, queue state, drops, underruns, limiting.
4. Add `vdsctl haptics-status --json`.
5. Add a regression test proving the current continuously renewed lease suppresses
   legacy rumble.
6. Do not remove the lease yet.

Acceptance:

- Policy and source state are inspectable.
- Existing failure is represented by a deterministic regression test.
- Protocol compatibility behavior is defined.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

## Step 6 — Add dedicated OpenDS5 haptics IPC stream

Agent use:

- Luna-medium complex implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Implement versioned control negotiation.
2. Implement bounded Unix `SOCK_SEQPACKET` data stream.
3. Associate stream with one port/controller.
4. Validate peer, version, identifiers, lengths, and format.
5. Change Linux helper output abstraction to send generated actuator PCM to IPC.
6. Keep capture/filtering behavior intact.
7. Stop production `pw-play` output into game rear channels.
8. Add disconnect, malformed-frame, overflow, and per-port tests.

Acceptance:

- Daemon distinguishes game PCM from OpenDS5 PCM.
- Helper crash deactivates only OpenDS5 source.
- Data path is nonblocking and bounded.
- Game audio path remains unaffected.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

### Step 6 implementation handoff

The daemon now exposes a credential-checked, nonblocking Unix
`SOCK_SEQPACKET` listener beside its control socket, negotiates the fixed v1
format, binds one stream owner to one virtual port, and stores validated frames
in a separate bounded OpenDS5 queue. Disconnect and port reload clear only that
source. The Linux helper keeps its existing PipeWire capture and filtering but
routes generated four-channel frames through the packaged
`vds-haptics-client`, which extracts the actuator pair; production helper
output no longer invokes `pw-play` on the game sink. Socket, malformed-frame,
overflow, sender-process, channel-extraction, and helper-routing paths are
covered automatically; disconnect and port-reload cleanup are implemented in
the daemon event loop. Step 7 must consume this queue in the
source-aware mixer; Step 6 intentionally does not alter game PCM output or the
lease-era physical arbitration.

The production-backed lifecycle registry covers client caps, expiry, per-port
ownership, disconnect cleanup, and reload. Final validation recorded 8/8 vDS
tests and 85 focused companion/typecheck checks; independent review is cleared
with no blocking findings. Hardware/live Bluetooth behavior remains
unvalidated. Step 7 carries P2 follow-ups for source-specific status semantics,
backpressure/drop observability, and sender allocation rationale/fix.

Detailed continuation state is recorded in
`docs/superpowers/plans/2026-07-30-haptics-step6-handoff.md`.

## Step 7 — Implement native-PCM source-aware mixer

Agent use:

- Luna-medium complex implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Add separate game and OpenDS5 PCM queues.
2. Align frames at the daemon output boundary.
3. Apply independent gains.
4. Mix in floating point.
5. Limit and quantize once.
6. Preserve speaker/headphone channels independently.
7. Implement `off`, `mix`, and native-PCM portion of `replace`.
8. Add source/policy matrix and clipping tests.

Acceptance:

- Native game PCM and OpenDS5 PCM coexist in `mix`.
- Game actuator PCM is absent in `replace`.
- `off` leaves game PCM untouched.
- Overload drops stale OpenDS5 data instead of delaying game audio.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

## Step 8 — Implement compatible-rumble synthesis and state arbitration

Agent use:

- Luna-medium complex implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Preserve game legacy motor state.
2. Keep native compatible-rumble mode when it is the only required source.
3. Add deterministic smoothed legacy-rumble PCM synthesis.
4. When PCM output is otherwise required, synthesize legacy state into the mix.
5. Add explicit state arbiter based on sources and policy.
6. Test transitions, motor updates, silence, policy changes, reconnect, and
   per-port isolation.
7. Document calibration requirements; do not assert physical equivalence yet.

Acceptance:

- Legacy game rumble coexists with OpenDS5 PCM in `mix`.
- Legacy game rumble is suppressed only in `replace`.
- No unnecessary conversion occurs for legacy-only game output.
- State transitions do not click, flap, or leak across ports in automated tests.

The legacy-rumble synthesizer uses deterministic per-port smoothing and no
runtime allocation. Its gain and time response are intentionally provisional:
hardware calibration is required before asserting equivalence with native
motor drive.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

## Step 7 closure

Step 7 now uses a source-aware mixer with independent gains, limiter, and one
final quantization step. The dedicated app source is resampled statefully from
48 kHz to 3 kHz (16:1 box average; 512 input frames produce one 64-byte stereo
actuator block). A fixed-capacity ring handles cross-packet continuity,
overflow, stale partials, and backpressure without blocking game output. Output
is data-driven with a 10 ms minimum Bluetooth throttle; `off`, `mix`, and
native-PCM `replace` preserve speaker payloads and leave HapticLease/legacy
semantics unchanged. Status reports source drops and limiting without
double-counting.

Validation recorded: vDS build and deterministic tests pass (endpoint socket
test requires host socket permission); companion focused typecheck/tests pass.
Live Bluetooth, hardware feel, and suspend/resume remain unvalidated. Step 8
is next.

## Step 9 — Remove the 100 ms ownership lease

Agent use:

- Luna-medium complex implementer.
- Terra-low tester.
- Terra-medium reviewer.

Actions:

1. Remove `HapticLease` and global `haptic_audio_active_` ownership semantics.
2. Replace all derived behavior with explicit source/policy arbitration.
3. Keep only source-specific liveness/watchdog behavior.
4. Remove or rewrite lease-era tests.
5. Confirm idle nonzero/zero app behavior cannot change game ownership.
6. Run the full automated matrix.

Acceptance:

- No amplitude- or time-based global haptics owner remains.
- App source closure restores pure game behavior immediately.
- All policy/source/lifecycle tests pass.

Checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

## Step 10 — Integrated validation, documentation, and final review

Agent use:

- Terra-low tester for deterministic suite.
- Terra-high final reviewer.
- One Luna-medium implementer only for accepted fixes.
- Rerun tester after fixes.

Actions:

1. Run full Nix, daemon, companion, config, and hygiene suites.
2. Run NixOS VM integration check.
3. Verify package output ownership.
4. Verify normal GUI launch contains no Polkit/service replacement path.
5. Review protocol and architecture documents against actual code.
6. Execute manual hardware matrix when hardware is available.
7. Specifically test Expedition 33 in `off`, `mix`, and `replace`.
8. Record unperformed hardware tests honestly.
9. Run final Graphify structural update.

Acceptance:

- Automated checks pass or only documented preexisting/environment failures
  remain.
- Final reviewer has no unresolved blocking finding.
- Documentation matches behavior.
- Hardware claims are evidence-backed.
- Unrelated worktree changes remain preserved.

Final checkpoint:

```sh
scripts/dev/graphify-checkpoint
```

### Step 10 result / handoff

Integrated deterministic validation passed after the reviewed fixes:

- vDS CTest: 9/9 passed.
- Companion transport tests, typecheck, and production build passed.
- NixOS module evaluation and `nix flake check -L` passed.
- Package ownership, normal-launch, capability-handshake, and WirePlumber
  registration checks passed.
- Final independent review has no blocking findings.
- The final Graphify checkpoint passed and rebuilt the repository graph.

One P2 test-quality follow-up remains documented: the app-close regression test
models the recomputed output-state transition but does not drive the actual
negotiated-stream HUP/error cleanup path end to end. No live Bluetooth,
controller, suspend/resume, or hardware-feel validation was performed,
including Expedition 33 in `off`, `mix`, and `replace`; no physical equivalence
claim is made.

## Codex start command

Use `docs/plans/CODEX-RUN-PROMPT.md`. Do not paste this entire plan into every
worker prompt.
