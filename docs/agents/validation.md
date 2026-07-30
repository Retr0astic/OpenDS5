# Validation Commands

Enter the repository environment first:

```sh
nix develop
```

Use existing canonical scripts when they differ from these examples.

## Repository hygiene

```sh
git status --short
git diff --check
```

## Nix

```sh
nix flake check -L
nix build -L .#opends5 .#vds
nix eval .#nixosModules.default
```

For module changes, run the repository's NixOS evaluation or VM test. Verify
actual output paths and service definitions, not only evaluation success.

## vDS

```sh
cmake -S vds -B build/vds -G Ninja -DBUILD_TESTING=ON
cmake --build build/vds
ctest --test-dir build/vds --output-on-failure
```

## Companion

```sh
npm --prefix ds5-bridge/companion run typecheck
npm --prefix ds5-bridge/companion run test:companion
npm --prefix ds5-bridge/companion run build:app
```

## Configuration files

```sh
scripts/dev/validate-codex-config
```

## Graphify

At the end of every numbered implementation step:

```sh
scripts/dev/graphify-checkpoint
```

## Manual hardware evidence

Record separately:

- Controller model and connection mode.
- Steam Input state.
- Game and exact test scene/action.
- Policy selected.
- Expected and observed game haptics.
- Expected and observed OpenDS5 haptics.
- Reconnect/crash behavior.
- Relevant `vdsctl haptics-status --json` snapshot.

A build or unit test is not hardware evidence.
