
## 1. Objective

Make the DualSense PS button substantially more useful for Linux gaming by turning it into a configurable gaming shortcut layer.

The feature should support:

- PS single press
- PS double press
- PS long press
- PS + button chords
- Global bindings
- Per-game overrides
- Linux-native action providers
- Safe passthrough behavior that does not initially break Steam Input, native DualSense support, or the OpenDS5 virtual-controller bridge

The first implementation must integrate with the existing OpenDS5 input, settings, game-watcher, IPC, and renderer architecture rather than creating a parallel remapping system.

---

## 2. Repository Context

OpenDS5 currently provides:

- A Linux Electron companion application
- DualSense input access through evdev
- Button remapping, personas, and chords
- Per-game profile detection
- A virtual wired DualSense through `vds`
- TypeScript and Vitest coverage in the companion application

Relevant area:

```text
ds5-bridge/companion/
├── src/main/
├── src/renderer/
├── src/shared/
└── package.json
```

The current evdev input reader recognizes only a subset of buttons. It must be extended to normalize at least:

- PS / Guide
- Create
- Options
- Touchpad click
- Mute
- D-pad directions
- Existing face and shoulder buttons

Before changing mappings, verify the actual Linux input codes from kernel headers and, where practical, from a real DualSense using `evtest`.

---

## 3. Product Scope

### 3.1 User-facing name

Use **Gaming Shortcuts** as the settings section name.

Avoid presenting this as only “PS button remapping.” The PS button is the entry point to a broader controller-driven Linux gaming action system.

### 3.2 Initial gestures

Support:

1. Single press
2. Double press
3. Long press
4. PS + one secondary button chord

The internal design should permit multi-button chords later, but the first UI may limit bindings to PS plus one secondary button.

### 3.3 Initial action types

Implement a typed action model. Initial actions:

- No action
- Pass through only
- Open or focus an application
- Execute a predefined desktop action
- Take screenshot
- Toggle recording
- Toggle performance HUD
- Volume up
- Volume down
- Mute audio
- Toggle microphone mute
- Open on-screen keyboard
- Switch to next application
- Switch to previous application
- Gracefully close the detected game
- Open OpenDS5
- Custom executable with an argument array

Do not implement arbitrary shell command strings using `shell: true`.

### 3.4 Suggested default preset

Do not silently enable destructive actions.

Recommended optional Linux Gaming preset:

| Gesture | Default action |
|---|---|
| PS single press | Open/focus OpenDS5 |
| PS double press | Switch to previous application |
| PS long press | No action initially |
| PS + Create | Screenshot |
| PS + Triangle | Toggle performance HUD |
| PS + Touchpad | Open on-screen keyboard |
| PS + D-pad Up | Volume up |
| PS + D-pad Down | Volume down |
| PS + D-pad Left | Previous application |
| PS + D-pad Right | Next application |
| PS + Mute | Toggle system microphone |
| PS + Options | Show quit-game confirmation |

Defaults should be offered as a preset and remain editable.

---

## 4. Non-goals for the First PR

Do not include these in the first implementation PR:

- A full PlayStation-style overlay
- Power-off, reboot, logout, or suspend actions
- Exclusive evdev grabbing
- Filtering the PS button inside `vdsd`
- Guaranteed suppression of the PS event before Steam or games receive it
- Broad support for every desktop environment
- Runtime installation of third-party screenshot or recording tools
- Game-specific plugin APIs
- Network or remote-control actions

These can follow after the input and action abstractions are stable.

---

## 5. Architecture

## 5.1 Shared controller button model

Create or extend a shared normalized controller-button type.

Example:

```ts
export type ControllerButton =
  | 'cross'
  | 'circle'
  | 'square'
  | 'triangle'
  | 'l1'
  | 'r1'
  | 'l2'
  | 'r2'
  | 'l3'
  | 'r3'
  | 'create'
  | 'options'
  | 'ps'
  | 'touchpad'
  | 'mute'
  | 'dpad-up'
  | 'dpad-down'
  | 'dpad-left'
  | 'dpad-right';
```

Requirements:

- One canonical type shared by main, renderer, persistence, and tests
- No duplicated free-form string unions
- Unknown evdev codes must be ignored safely
- Existing trigger-modifier behavior must remain compatible

## 5.2 Extend evdev input normalization

Update the Linux evdev reader to recognize the missing DualSense buttons.

Requirements:

- Verify Linux event codes rather than guessing
- Keep device discovery restricted to the actual DualSense gamepad node
- Preserve existing trigger-axis handling
- Preserve synchronization on `EV_SYN`
- Add tests for press, release, held state, split buffers, and unknown codes
- Do not put gesture timing logic in the evdev reader

The reader’s responsibility ends at emitting normalized controller state or normalized button transitions.

## 5.3 Gesture engine

Add a dedicated, platform-independent gesture engine.

Suggested location:

```text
src/main/gaming-shortcuts/
├── gesture-engine.ts
└── gesture-engine.test.ts
```

Suggested events:

```ts
export type ControllerGesture =
  | { type: 'single-press'; button: ControllerButton }
  | { type: 'double-press'; button: ControllerButton }
  | { type: 'long-press'; button: ControllerButton; durationMs: number }
  | { type: 'chord'; modifier: ControllerButton; button: ControllerButton };
```

Responsibilities:

- Rising-edge detection
- Falling-edge detection
- Debouncing
- Double-press timing
- Long-press timing
- Chord recognition
- Per-controller state
- Conflict resolution
- Timer cleanup on stop/disconnect
- Deterministic operation under fake timers

Recommended defaults:

```ts
doublePressWindowMs = 300
longPressThresholdMs = 650
chordWindowMs = 150
```

Make thresholds configurable within bounded ranges.

### Gesture precedence

Use this precedence:

1. Recognized chord
2. Long press
3. Double press
4. Single press

A pending single press must be delayed until the double-press window expires.

When a PS chord fires:

- Do not also emit PS single press
- Do not emit a secondary-button action from this subsystem
- Emit only one chord event for the same press cycle

Holding PS and repeatedly tapping another button may repeat the chord only after that secondary button is released and pressed again.

## 5.4 Action schema

Create a discriminated union in shared code.

Example:

```ts
export type GamingShortcutAction =
  | { type: 'none' }
  | { type: 'passthrough' }
  | { type: 'open-opends5' }
  | { type: 'launch-app'; executable: string; args: string[] }
  | { type: 'focus-app'; appId: string }
  | { type: 'volume'; direction: 'up' | 'down' | 'mute' }
  | { type: 'microphone-mute-toggle' }
  | { type: 'screenshot'; provider: CaptureProvider }
  | { type: 'recording-toggle'; provider: CaptureProvider }
  | { type: 'performance-hud-toggle'; provider: HudProvider }
  | { type: 'on-screen-keyboard'; provider: KeyboardProvider }
  | { type: 'switch-application'; direction: 'next' | 'previous' }
  | { type: 'quit-active-game'; confirmation: true }
  | { type: 'custom-executable'; executable: string; args: string[] };
```

Validate persisted data at load time. Unknown or malformed actions must become `{ type: 'none' }`, not crash application startup.

## 5.5 Action executor

Suggested location:

```text
src/main/gaming-shortcuts/
├── action-executor.ts
├── action-executor.test.ts
├── process-runner.ts
└── providers/
```

Rules:

- Use `spawn`/`execFile` with an argument array
- Do not use `shell: true`
- Add execution timeouts where applicable
- Log failures without crashing the controller input loop
- Avoid blocking the Electron main process
- Return structured success/failure results
- Never interpolate user input into shell text
- Require UI confirmation before saving a destructive or custom action where appropriate

## 5.6 Provider abstraction

Model capabilities instead of hard-coding one desktop.

Suggested providers:

```text
providers/
├── detect-environment.ts
├── generic-linux.ts
├── hyprland.ts
├── kde.ts
├── gnome.ts
├── sway.ts
├── gamescope.ts
├── screenshot.ts
├── recording.ts
├── hud.ts
└── on-screen-keyboard.ts
```

Initial implementation priority:

1. Generic Linux
2. Hyprland
3. KDE Plasma
4. GNOME
5. Sway
6. Gamescope-specific enhancements

Provider detection may inspect:

- `XDG_CURRENT_DESKTOP`
- `XDG_SESSION_DESKTOP`
- `XDG_SESSION_TYPE`
- `HYPRLAND_INSTANCE_SIGNATURE`
- `SWAYSOCK`
- `GAMESCOPE_WAYLAND_DISPLAY`
- Executable availability

Detection must be testable by injecting environment variables and executable probes.

### Generic fallbacks

Prefer desktop standards and portals when suitable. When no supported provider exists:

- Keep the binding visible
- Mark it unavailable
- Explain which executable or desktop integration is required
- Do not silently run an unrelated fallback

## 5.7 Settings and migration

Add versioned persisted settings.

Suggested schema:

```ts
export interface GamingShortcutsSettings {
  enabled: boolean;
  presetId: string | null;
  doublePressWindowMs: number;
  longPressThresholdMs: number;
  chordWindowMs: number;
  psButton: {
    singlePress: GamingShortcutAction;
    doublePress: GamingShortcutAction;
    longPress: GamingShortcutAction;
  };
  chords: Array<{
    button: Exclude<ControllerButton, 'ps'>;
    action: GamingShortcutAction;
  }>;
  perGameOverrides: Record<string, Partial<GameShortcutBindings>>;
}
```

Requirements:

- Preserve all existing settings
- Migration must be idempotent
- Missing settings receive safe defaults
- Unknown future fields should not corrupt existing data
- Reset-to-default must affect only Gaming Shortcuts when invoked from that section

## 5.8 Per-game integration

Reuse the existing game watcher and canonical game identity if one exists.

Resolution order:

1. Matching per-game override
2. Global Gaming Shortcuts settings
3. No action

Per-game override UI may be deferred to a second PR if needed, but the persisted model and resolver should be designed so it can be added without schema replacement.

Do not independently scan processes in the gesture engine.

## 5.9 Main/renderer IPC

Expose narrow IPC methods:

- Read Gaming Shortcuts settings
- Save validated settings
- Query available providers/capabilities
- Test a non-destructive action
- Receive execution result for UI feedback

Do not expose unrestricted process execution through IPC.

All IPC payloads must be validated in the main process.

## 5.10 Renderer UI

Add a **Gaming Shortcuts** settings page or section consistent with `UI_STYLE_GUIDE.md`.

Sections:

1. Enable Gaming Shortcuts
2. PS Button
   - Press
   - Double press
   - Hold
3. PS Chords
4. Timing
5. Environment and provider status
6. Presets
7. Per-game overrides, if included in the selected PR scope

UI requirements:

- Controller button labels and icons where existing components permit
- Clearly show unavailable actions
- Explain Steam/Guide-button passthrough limitations
- Confirmation for quit-game actions
- Custom executable editor uses separate executable and arguments fields
- No raw shell text field
- Keyboard-accessible controls
- No destructive default binding

---

## 6. PS Button Passthrough and Steam Compatibility

The first version is observational and action-triggering.

It must not claim to consume or suppress the PS button unless that behavior is proven across:

- Steam Input enabled
- Steam Input disabled
- Native DualSense games
- Bluetooth physical controller
- OpenDS5 virtual wired controller
- Gamescope
- Normal desktop sessions

UI copy should state that Steam or a game may also receive the PS button.

Do not use `EVIOCGRAB` in the first PR.

A later design investigation should compare:

1. Exclusive physical evdev grab
2. Selective event filtering in `vdsd`
3. Virtual-controller report rewriting
4. Steam-compatible Guide-button passthrough rules

That investigation must be a separate issue or plan.

---

## 7. Quit-game Safety

`quit-active-game` must not immediately send SIGKILL.

Preferred sequence:

1. Resolve the active game through existing game-watcher data
2. Display confirmation
3. Request graceful termination
4. Report whether termination was requested successfully
5. Offer force termination only as a separate explicit action later

Never infer a PID from untrusted text.

---

## 8. Testing Strategy

## 8.1 Unit tests

Add tests for:

### Evdev reader

- Every newly supported button code
- Press and release
- Held state across synchronization frames
- Unknown codes
- Partial event buffers
- Multiple events in one buffer
- Stop/reset behavior

### Gesture engine

- Single press after timeout
- Double press suppresses single press
- Long press suppresses single press
- Chord suppresses PS single press
- Chord order tolerance within configured window
- Secondary button must be re-pressed before repeat
- Timer cleanup
- Disconnect/reset
- Multiple controller IDs remain isolated, if IDs are available
- Boundary timing values
- Invalid timing configuration is clamped or rejected

Use fake timers; do not rely on real sleeps.

### Action validation

- Valid action variants
- Unknown action type
- Missing required fields
- Invalid executable
- Invalid argument arrays
- Migration from settings without Gaming Shortcuts
- Future/unknown fields

### Action executor

Mock process execution.

Test:

- Correct executable and argument array
- No shell invocation
- Missing dependency
- Provider unavailable
- Process failure
- Timeout
- Repeated shortcut throttling where needed
- Error does not crash subsequent actions

### Provider detection

Inject environment and executable availability.

Test Hyprland, KDE, GNOME, Sway, Gamescope, generic Wayland, and generic X11 detection.

## 8.2 Integration tests

- Settings load → gesture → resolved binding → action executor
- Global binding
- Per-game override resolution
- Disabled feature
- Provider unavailable
- Renderer settings save through validated IPC

## 8.3 Manual test matrix

Test with a real DualSense:

| Scenario | Required |
|---|---|
| Bluetooth controller through OpenDS5/vds | Yes |
| Wired physical DualSense | Where supported |
| Steam Input enabled | Yes |
| Steam Input disabled | Yes |
| Native DualSense game | Yes |
| Non-native game | Yes |
| Hyprland Wayland | Yes |
| At least one non-Hyprland desktop | Yes before broad support claims |
| PS single/double/hold/chords | Yes |
| Disconnect during pending timer | Yes |
| App restart with saved bindings | Yes |

Record actual evdev codes observed during testing in the PR description.

---

## 9. Delivery Strategy

Use several reviewable pull requests rather than one large branch.

### PR 1 — Input model and gesture engine

- Shared button type
- Missing DualSense evdev buttons
- Gesture engine
- Comprehensive unit tests
- No user-facing actions yet, except an optional debug/test harness

Acceptance criteria:

- PS, Create, Options, Touchpad, Mute, and D-pad are normalized correctly
- Gesture engine passes deterministic fake-timer tests
- Existing behavior remains green
- No event grabbing or suppression

### PR 2 — Action schema, executor, and providers

- Typed actions
- Validation
- Generic provider capability model
- Hyprland provider
- Safe process runner
- Unit tests

Acceptance criteria:

- No shell-based execution
- Missing providers fail gracefully
- Actions do not block or crash input processing

### PR 3 — Settings and UI

- Gaming Shortcuts page
- Global bindings
- Preset
- Timing controls
- Provider availability
- IPC validation
- Settings migration

Acceptance criteria:

- Existing users retain settings
- Invalid persisted data is repaired safely
- No destructive default mapping
- UI follows the repository style guide

### PR 4 — Per-game overrides

- Reuse game watcher
- Override resolver
- Per-game UI
- Tests

### PR 5 — Overlay, optional

Only after the shortcut system is stable:

- Lightweight controller-navigable overlay
- Current game/profile
- Audio, capture, and quit controls

### Separate investigation — PS event suppression

Do not combine this with the initial feature series.

---

## 10. Definition of Done

A phase is complete when:

- TypeScript typecheck passes
- Relevant Vitest suites pass
- Existing companion tests pass
- Build succeeds
- New behavior has unit tests
- Manual DualSense validation is documented
- Settings migration is tested
- No arbitrary shell execution is introduced
- No regressions to native DualSense behavior are observed
- Documentation explains limitations and provider requirements
- PR is narrowly scoped and reviewable

Expected commands, verify against the current repository:

```bash
cd ds5-bridge/companion
npm ci
npm run typecheck
npm run test:companion
npm run build:app
```

Do not run Windows-only or firmware test targets on Linux unless their prerequisites are available. Report exactly which checks were and were not run.

---

## 11. Open Questions for the Maintainer

Confirm before implementation:

1. Should the first PR contain only input normalization and gesture recognition?
2. Is the existing remapping/chord engine intended to own this behavior, or should Gaming Shortcuts use a dedicated gesture layer that feeds the same action resolver?
3. What is the canonical persisted game identifier?
4. Should wired physical DualSense input be supported by this feature, or only controllers routed through OpenDS5/vds?
5. Which desktop environment should be considered the first officially supported provider?
6. Should custom executable actions be allowed in the initial release?
7. Is preserving Steam’s Guide-button behavior the default policy?
8. Does the maintainer prefer one feature branch with stacked PRs or separate branches from `main`?

---

## 12. Recommended First Issue

Title:

```text
Feature: configurable PS-button gestures for Linux gaming shortcuts
```

Issue summary:

```text
Add normalized PS/Create/Options/Touchpad/Mute/D-pad input and a tested gesture
engine for PS single press, double press, long press, and PS chords. The first
phase will remain passthrough-only and will not grab evdev or suppress Steam/game
Guide-button events. Later PRs will add typed Linux actions, settings UI, and
per-game overrides.
```
