{
  lib,
  stdenv,
  buildNpmPackage,
  electron_42,
  nodejs,
  libusb1,
  makeWrapper,
  pipewire,
  glib,
  vds,
  version,
}:
buildNpmPackage {
  pname = "opends5";
  inherit version;

  # Keep the companion tree while including only its shared controller/glyph
  # assets required by renderer imports and package metadata.
  src = lib.cleanSourceWith {
    src = ../.;
    filter = path: type:
      let
        rel = lib.removePrefix (toString ../.) (toString path);
      in
        rel == "/ds5-bridge"
        || rel == "/ds5-bridge/assets"
        || lib.hasPrefix "/ds5-bridge/assets/" rel
        || rel == "/ds5-bridge/companion"
        || lib.hasPrefix "/ds5-bridge/companion/" rel;
  };

  npmDepsHash = "sha256-fs26+WmbKvSNUruvQ/dpDJpVv2NZ+5IMOoBKW+KScV8=";

  postPatch = ''
    cp ds5-bridge/companion/package.json ./package.json
    cp ds5-bridge/companion/package-lock.json ./package-lock.json
  '';

  env = {
    ELECTRON_SKIP_BINARY_DOWNLOAD = "1";
  };

  nativeBuildInputs = [
    makeWrapper
  ];

  dontNpmBuild = true;

  buildPhase = ''
    runHook preBuild

    pushd ds5-bridge/companion
    # npm's generated .bin shims use /usr/bin/env, which is not available in
    # the Nix sandbox. Invoke the tools with the pinned Nix Node.js directly.
    ${nodejs}/bin/node ../../node_modules/typescript/bin/tsc -p tsconfig.main.json
    ${nodejs}/bin/node ../../node_modules/vite/bin/vite.js build
    popd

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p "$out/bin"
    mkdir -p "$out/share/opends5"
    mkdir -p "$out/share/opends5/node_modules"
    mkdir -p "$out/share/opends5/native"

    companion="$PWD/ds5-bridge/companion"
    cp -r "$companion/dist" \
      "$out/share/opends5/dist"

    cp "$companion/package.json" \
      "$out/share/opends5/package.json"

    cp -r "$PWD/node_modules/node-hid" \
      "$out/share/opends5/node_modules/node-hid"

    cp -r "$PWD/node_modules/pkg-prebuilds" \
      "$out/share/opends5/node_modules/pkg-prebuilds"

    cp "$companion/native/audio-helper-linux.mjs" \
      "$out/share/opends5/native/audio-helper-linux.mjs"

    cp "$companion/src/renderer/assets/test-speaker-tone-silence-tail.mp3" \
      "$out/share/opends5/native/test-speaker-tone-silence-tail.mp3"

    makeWrapper ${electron_42}/bin/electron "$out/bin/opends5" \
      --set-default OPENDS5_APP_ROOT \
        "$out/share/opends5" \
      --prefix LD_LIBRARY_PATH : "${lib.makeLibraryPath [
      libusb1
      stdenv.cc.cc.lib
      glib
    ]}" \
      --prefix PATH : "${lib.makeBinPath [
      pipewire
      vds
    ]}" \
      --add-flags "$out/share/opends5"

    runHook postInstall
  '';
  meta = {
    description = "OpenDS5 DualSense companion application";
    homepage = "https://github.com/LordVicky/OpenDS5";
    license = lib.licenses.agpl3Only;
    mainProgram = "opends5";
    platforms = lib.platforms.linux;
  };
}
