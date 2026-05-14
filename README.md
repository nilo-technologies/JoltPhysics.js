**Package:** [`@nilo-technologies/jolt-physics`](https://github.com/orgs/nilo-technologies/packages) (GitHub Packages; built from this repo).

[![Build Status](https://github.com/nilo-technologies/JoltPhysics.js/actions/workflows/build-and-deploy.yml/badge.svg)](https://github.com/nilo-technologies/JoltPhysics.js/actions/)

# Nilo fork — working in this repo

This is Nilo's private fork of `JoltPhysics.js`. We do not merge back upstream; the workflows in this section are the authoritative ones for working in this repo. The upstream README is preserved below ([jump to it](#joltphysicsjs-upstream-readme)) for API reference.

## Repository layout (sibling clones)

The build scripts use a sibling-clone convention. Put these repos in the same parent folder:

| Folder | Purpose | Repo |
|---|---|---|
| `JoltPhysics.js/` | This repo. WASM bindings + npm package. | `nilo-technologies/JoltPhysics.js` |
| `JoltPhysics/`    | Nilo's C++ Jolt fork. Linked into this build via `-DJOLT_PHYSICS_PATH`. | `nilo-technologies/JoltPhysics` |
| `emsdk/`          | Emscripten SDK (compiles C++ → WASM). | `emscripten-core/emsdk` |
| `Nilo/`           | The Nilo client. Consumes the published package; can also point at a local build of this repo. | _private_ |

If you keep this layout, `iter-build-windows.ps1` and `build-windows.ps1` auto-detect everything. To use a different layout, set `JOLT_PHYSICS_PATH` and `EMSDK` environment variables.

**Tag convention:** both `JoltPhysics` and `JoltPhysics.js` use `nilo-vX.Y.Z` git tags on paired commits (`X.Y.Z` is the upstream Jolt C++ API the fork is paired with). Build scripts default to `nilo-v5.5.0`; override with `$env:NILO_JOLT_TAG`.

## One-time setup

1. Install **CMake** (>= 3.20), **Ninja**, and **Node.js** (>= 18) on PATH.
2. Clone the Emscripten SDK:
   ```powershell
   git clone https://github.com/emscripten-core/emsdk C:\dev\emsdk
   cd C:\dev\emsdk
   .\emsdk.ps1 install latest
   .\emsdk.ps1 activate latest
   ```
3. Clone Nilo's Jolt C++ fork:
   ```powershell
   git clone https://github.com/nilo-technologies/JoltPhysics C:\dev\JoltPhysics
   cd C:\dev\JoltPhysics
   git checkout nilo-v5.5.0
   ```
4. Clone this repo on the `nilo` branch:
   ```powershell
   git clone -b nilo https://github.com/nilo-technologies/JoltPhysics.js C:\dev\JoltPhysics.js
   ```

## Fast C++ iteration loop (daily-driver flow)

When you want to make a C++ change in `C:\dev\JoltPhysics`, build it locally, and run Nilo against your build without re-publishing the npm package:

```powershell
# from C:\dev\Nilo
just jolt-iter                          # Debug, closure on, ~30-60 s/iter
just jolt-iter "-FastLink"              # closure off, ~5-20 s/iter (Debug-safe)
just jolt-iter "-Variant Release"       # build the wasm-compat publish-format

# Equivalent, run directly from C:\dev\JoltPhysics.js:
.\iter-build-windows.ps1
.\iter-build-windows.ps1 -FastLink
.\iter-build-windows.ps1 -Variant Release
```

What it does:

* Configures one Ninja build dir at `Build\Iter\<Variant>\ST` and reuses it across runs (no `rm -rf dist`; incremental rebuilds stay valid).
* Builds a single target: `jolt-wasm` for Debug, `jolt-wasm-compat` for Release.
* Emits the result into `dist\` so Nilo's Vite alias can pick it up.

**Output layout** depends on `-Variant`:

| Variant | Output files | Format | Used by |
|---|---|---|---|
| Debug   | `dist\jolt-physics.wasm.js` + `dist\jolt-physics.wasm.wasm` | non-compat (separate `.wasm` sidecar) | C++ debugging in Chrome DevTools |
| Release | `dist\jolt-physics.wasm-compat.js`                          | wasm-compat (single file, embedded WASM) | matches the publish-quality format |

Why the asymmetry: the wasm-compat single-file format embeds the entire WASM (~30 MB with DWARF) as one base64 string literal in the JS. The resulting ~42 MB JS source crashes Chrome DevTools' renderer when setting C++ breakpoints (the DWARF extension has to keep the WASM bytes, JS source, and DWARF index resident simultaneously). The non-compat form keeps the JS glue ~1 MB and exposes the WASM as a first-class binary resource that DevTools handles natively. So we use non-compat for Debug iter and wasm-compat for Release iter (matches what Nilo actually loads in production).

### Wiring an iter build into Nilo

One-time, add to `C:\dev\Nilo\.env.local`:  

```env
NILO_JOLT_LOCAL_DIST=C:/dev/JoltPhysics.js/dist
NILO_JOLT_DEBUG=true    # must match the variant of your last iter build
```

Then:

```powershell
cd C:\dev\Nilo
pnpm dev    # Vite reads NILO_JOLT_DEBUG / NILO_JOLT_LOCAL_DIST from .env.local
```

`vite.config.js` re-aliases `jolt-physics` on dev-server start, disables `optimizeDeps` for it, and extends `server.fs.allow` so the `.wasm.wasm` sidecar fetch works. Subsequent re-runs of `iter-build-windows.ps1` do **not** require a Vite restart — just hard-refresh the browser tab.

`NILO_JOLT_LOCAL_DIST` is the source of truth for the local-iter loop. When it's set, the admin-only "Debug" toggle in Nilo's Dev Menu → Physics auto-hides (it would otherwise race with the env var). For the **published-package** flow (no `NILO_JOLT_LOCAL_DIST`), flipping that toggle dynamic-imports `jolt-physics/debug-wasm` from `node_modules` instead of the Release entrypoint, so debug bytes are only downloaded when explicitly requested.

### C++ source resolution in Chrome DevTools

Install the **C/C++ DevTools Support (DWARF)** extension. Open its options, and under **Path substitutions** add (order matters; more-specific first):

| Source path (in DWARF) | Local path |
|---|---|
| `JoltPhysics.js/` | `C:\dev\JoltPhysics.js\` |
| `JoltPhysics/`    | `C:\dev\JoltPhysics\` |

These work for both local-iter builds **and** the published debug package. `CMakeLists.txt` applies `-fdebug-prefix-map` / `-fmacro-prefix-map` from the real FetchContent root (`joltphysics_SOURCE_DIR`, not the mis-cased `JoltPhysics_SOURCE_DIR`, which broke Linux CI), plus `-fdebug-compilation-dir=.` so DWARF does not leak the absolute CMake binary directory. A final map collapses any leftover ``JoltPhysics.js/JoltPhysics/`` nested prefix to ``JoltPhysics/``. With that, DWARF records portable paths regardless of build host, and the substitutions above resolve them on any machine.

## Publishing to GitHub Packages

Publishing is **CI-only**. Don't `npm publish` from a workstation.

1. **Bump the version** in `package.json`. The scheme is `MAJOR.MINOR.PATCH-nilo.N`:
   * `MAJOR.MINOR.PATCH` mirrors the upstream Jolt C++ API line (paired with git tag `nilo-vMAJOR.MINOR.PATCH` on both repos).
   * `nilo.N` is our fork counter — bump for any fork-only change (CI, packaging, binding tweaks).
   ```powershell
   # Fork-counter bump (e.g. 5.5.0-nilo.0 -> 5.5.0-nilo.1):
   npm version prerelease --preid=nilo --no-git-tag-version

   # Move to a new Jolt C++ API pin (e.g. 5.5.0 -> 5.6.0):
   npm version 5.6.0-nilo.0 --no-git-tag-version
   ```
   npm only allows three numeric segments, so the fourth counter has to live in the prerelease tag.
2. **Tag both repos** with the new pair (push `nilo-vX.Y.Z` to `nilo-technologies/JoltPhysics` first, then to this repo) and verify the C++ tag is reachable on GitHub. The Action checks out the C++ ref with `fetch-depth: 0` so tags resolve reliably, but the tag has to exist.
3. **Trigger the workflow:** GitHub → **Actions** → **Build and Deploy** → **Run workflow**.
   * Leave **dry run** ON to produce a `jolt-physics-dist` artifact for inspection without publishing.
   * Turn **dry run** OFF to publish. The Action also creates a **GitHub Release** with the `dist/` tarball attached — handy for symbolication and pinning old debug bundles.

## Consuming the package from Nilo

The published artifacts are wired into the Nilo client via:

* Root `package.json` and `packages/physics-with-jolt/package.json` alias `jolt-physics` → `npm:@nilo-technologies/jolt-physics@5.5.0-nilo.N`. Bump both when you publish.
* Root `.npmrc` maps `@nilo-technologies` → `https://npm.pkg.github.com`.

# JoltPhysics.js (upstream README)

This project enables using [Jolt Physics](https://github.com/nilo-technologies/JoltPhysics) (Nilo's C++ fork) in JavaScript.

When CMake **FetchContent** is used (no `-DJOLT_PHYSICS_PATH`), it pulls that fork by default: **`JOLT_PHYSICS_GIT_REPO=https://github.com/nilo-technologies/JoltPhysics`** and tag **`nilo-v5.5.0`**. Override with `-DJOLT_PHYSICS_GIT_REPO` / `-DJOLT_PHYSICS_GIT_TAG`, or point at a local clone with `-DJOLT_PHYSICS_PATH`.

## Demos

Go to the [demos page](https://jrouwe.github.io/JoltPhysics.js/) to see the project in action.

## Using

This library comes in 6 flavours:
- `wasm-compat` - A WASM version with the WASM file (encoded in base64) embedded in the bundle
- `wasm` - A WASM version with a separate WASM file
- `debug-wasm` - Same as `wasm` (separate `.wasm` sidecar) but compiled with DWARF + assertions for C++ source-level debugging. The Nilo fork dropped the upstream `debug-wasm-compat` flavour because the giant base64 WASM string embedded in JS crashed Chrome DevTools when setting C++ breakpoints; non-compat keeps JS glue ~1 MB and exposes the WASM as a first-class binary that the C/C++ DWARF extension handles natively.
- `asm` - A JavaScript version that uses [asm.js](https://developer.mozilla.org/en-US/docs/Games/Tools/asm.js)
- `wasm-compat-multithread` - Same as `wasm-compat` but with multi threading enabled.
- `wasm-multithread` - Same as `wasm` but with multi threading enabled.

See [falling_shapes.html](Examples/falling_shapes.html) for an example on how to use the library.

### Documentation

The interface of the library is the same as the C++ interface of JoltPhysics, this means that you can use the [C++ documentation](https://jrouwe.github.io/JoltPhysics/) as reference.

Almost the entire Jolt interface has been exposed. Check [JoltJS.idl](https://github.com/jrouwe/JoltPhysics.js/blob/main/JoltJS.idl) if a particular interface has been exposed. If not, edit [JoltJS.idl](https://github.com/jrouwe/JoltPhysics.js/blob/main/JoltJS.idl) and [JoltJS.h](https://github.com/jrouwe/JoltPhysics.js/blob/main/JoltJS.h) and send a pull request, or open an issue.

### Installation

This library is distributed as ECMAScript modules on **GitHub Packages** as **`@nilo-technologies/jolt-physics`**. Add to [`.npmrc`](.npmrc) (or your user config): `@nilo-technologies:registry=https://npm.pkg.github.com`, then authenticate ([GitHub npm registry](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-npm-registry)). Install:

```sh
npm install @nilo-technologies/jolt-physics@nilo
```

The **`nilo`** [dist-tag](https://docs.npmjs.com/cli/v10/commands/npm-dist-tag) points at the latest **`…-nilo.*`** prerelease from CI (required by npm when publishing prereleases). You can still pin an exact version, e.g. **`@nilo-technologies/jolt-physics@5.5.0-nilo.0`**.

The upstream project also publishes **`jolt-physics`** on the public npm registry; this fork uses a scoped name for org publishing.

### Versioning (Nilo fork)

npm allows only **three** numeric segments (`MAJOR.MINOR.PATCH`). The published version uses a **prerelease** segment as a fourth counter: **`5.5.0-nilo.0`**, **`5.5.0-nilo.1`**, …

- **`5.5.0`** matches the **Jolt C++ API** line you pair with git tag **`nilo-v5.5.0`** (bump this when you move to e.g. **`nilo-v5.6.0`** → **`5.6.0-nilo.0`**).
- **`nilo.N`** increments for **fork-only** changes (CI, packaging, bindings tweaks) without changing the Jolt API pin.

Bump only the fork counter before republishing the same API pin (must already include `-nilo.*`):

```sh
npm version prerelease --preid=nilo --no-git-tag-version
```

That turns **`5.5.0-nilo.0`** → **`5.5.0-nilo.1`**. To introduce the first prerelease from a plain **`5.5.0`**, set the version explicitly (because plain **`npm version prerelease`** would bump patch, not add **`-nilo.0`**):

```sh
npm version 5.5.0-nilo.0 --no-git-tag-version
```

The different flavours are available via entrypoints on the npm package:

```js
// WASM embedded in the bundle
import Jolt from '@nilo-technologies/jolt-physics';
import Jolt from '@nilo-technologies/jolt-physics/wasm-compat';

// WASM
import Jolt from '@nilo-technologies/jolt-physics/wasm';

// WASM with DWARF + assertions for C++ source-level debugging (separate .wasm sidecar)
import Jolt from '@nilo-technologies/jolt-physics/debug-wasm';

// asm.js
import Jolt from '@nilo-technologies/jolt-physics/asm';

// WASM embedded in the bundle, multithread enabled
import Jolt from '@nilo-technologies/jolt-physics/wasm-compat-multithread';

// WASM, multithread enabled
import Jolt from '@nilo-technologies/jolt-physics/wasm-multithread';
```

You can also import esm bundles with unpkg (public **`jolt-physics`** package on npm; this fork’s GitHub Packages build is normally consumed via a bundler or `npm pack` tarball):

```html
<script type="module">
    // import latest
    import Jolt from 'https://www.unpkg.com/jolt-physics/dist/jolt-physics.wasm-compat.js';

    // or import a specific version
    import Jolt from 'https://www.unpkg.com/jolt-physics@x.y.z/dist/jolt-physics.wasm-compat.js';
</script>
```

Where ```x.y.z``` is the version of the library you want to use.

### Using the WASM flavour

To use the `wasm` flavour, you must either serve the WASM file `jolt-physics.wasm.wasm` alongside `jolt-physics.wasm.js`, or use a bundler that supports importing an asset as a url, and tell Jolt where to find the WASM file.

To specify where to retrieve the WASM file from, you can pass a `locateFile` function to the default export of `@nilo-technologies/jolt-physics/wasm`. For example, using [vite](https://vitejs.dev/) this would look like: 

```js
import initJolt from "@nilo-technologies/jolt-physics";
import joltWasmUrl from "@nilo-technologies/jolt-physics/jolt-physics.wasm.wasm?url";

const Jolt = await initJolt({
  locateFile: () => joltWasmUrl,
});
```

For more information on the `locateFile` function, see the [Emscripten documentation](https://emscripten.org/docs/api_reference/module.html#Module.locateFile).

## Building

This project has only been compiled under Linux.

* Install [emscripten](https://emscripten.org/) and ensure that its environment variables have been setup
* Install [cmake](https://cmake.org/)
* Run ```./build.sh``` to build both the Debug and Distribution build, ```./build.sh Debug``` for only the Debug build.
* **GitHub Actions** checks out [nilo-technologies/JoltPhysics](https://github.com/nilo-technologies/JoltPhysics) at the ref from **`JOLT_PHYSICS_CHECKOUT_REF`** / the **jolt_ref** input (default **`nilo-v5.5.0`**, keep in sync with **`JOLT_PHYSICS_GIT_TAG`** in CMake) and passes **`-DJOLT_PHYSICS_PATH`**. That ref must **exist on GitHub** (push your paired tag: `git push origin nilo-v5.5.0` on the Jolt repo, or pass **jolt_ref** `master` for a trial until tags are up). The Jolt checkout uses **`fetch-depth: 0`** so tags resolve reliably.
* **Trial run:** **Actions** → **Build and Deploy** → **Run workflow**; leave **dry run** on to get a **`jolt-physics-dist`** artifact only, or turn it off to publish.

Additional options that can be provided to ```build.sh```:

* ```-DENABLE_MEMORY_PROFILER=ON``` will enable memory tracking to detect leaks.
* ```-DDOUBLE_PRECISION=ON``` will enable the double precision mode. This allows worlds larger than a couple of km.
* ```-DENABLE_SIMD=ON``` will enable SIMD instructions. Safari 16.4 was the last major browser to support this (in March 2023). The multithreaded builds have this on by default.
* ```-DBUILD_WASM_COMPAT_ONLY=ON``` speeds up the build by only compiling the WASM compat version which the examples use.
* ```-DCROSS_PLATFORM_DETERMINISTIC=ON``` builds the library so that it produces the same results as the native version of the library. For more info [click here](https://jrouwe.github.io/JoltPhysics/#deterministic-simulation).

## Running

By default the examples use the WASM compat version of Jolt. This requires serving the html file using a web server rather than opening the html file directly.

Open a terminal in this folder and run the following commands:

```
npm install
npm run examples
```

Then navigate to: [http://localhost:3000/](http://localhost:3000/)

If you need to debug the C++ code take a look at [WASM debugging](https://developer.chrome.com/blog/wasm-debugging-2020/).

## Memory Management

The samples are very bad at cleaning up after themselves (basically they don't). When using emscripten to port a library to WASM, [nothing is cleaned up](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/WebIDL-Binder.html#using-c-classes-in-javascript) automatically, so everything you newed with ```new Jolt.XXX``` needs to be destroyed by ```Jolt.destroy(...)```.

On top of this, Jolt uses reference counting for a number of its classes (everything that inherits from [RefTarget](https://jrouwe.github.io/JoltPhysics/class_ref_target.html)). The most important classes are:

* ShapeSettings
* Shape
* ConstraintSettings
* Constraint
* PathConstraintPath
* PhysicsMaterial
* GroupFilter
* SoftBodySharedSettings
* VehicleCollisionTester
* VehicleControllerSettings
* WheelSettings
* CharacterBaseSettings
* CharacterBase
* Skeleton
* SkeletonAnimation
* SkeletonMapper
* PhysicsScene
* RagdollSettings
* Ragdoll

Reference counting objects start with a reference count of 0. If you want to keep ownership over the object, you need to call ```object.AddRef()```, this will increment the reference count. If you want to release ownership you call ```object.Release()```, this will decrement the reference count and if the reference count reaches 0 the object will be destroyed. If, after newing, you pass a reference counted object on to another object (e.g. a ShapeSettings to a CompoundShapeSettings or a Shape to a Body) then that other object will take a reference, in that case it is not needed take a reference yourself beforehand so you can skip the calls to ```AddRef/Release```. Note that it is also possible to do ```new Jolt.XXX``` followed by ```Jolt.destroy(...)``` for a reference counted object if no one took a reference.

The Body class is also a special case, it is destroyed through BodyInterface.DestroyBody(body.GetID()) (which internally destroys the Body).

Almost everything else can be destroyed straight after it has been passed to Jolt. [An example that shows how to properly clean up using Jolt is here](https://github.com/jrouwe/JoltPhysics.js/blob/main/Examples/proper_cleanup.html).

## Projects using JoltPhysics.js

* [Babylon.js plugin](https://github.com/PhoenixIllusion/babylonjs-jolt-physics-plugin) - A plugin that replaces the default physics engine with Jolt.
* [GDevelop](https://gdevelop.io/) - An Open-source, cross-platform 2D/3D/multiplayer game engine. See [announcement](https://github.com/4ian/GDevelop/releases/tag/v5.5.220).
* [react-three-jolt](https://github.com/pmndrs/react-three-jolt) - Wraps Jolt to make it easy to use in react-three-fiber.
* [r3f-jolt](https://github.com/sajal353/r3f-jolt) - Another wrapper for react-three-fiber.
* [Synthesis](https://github.com/Autodesk/synthesis) - A Robotics Simulator for Autodesk Fusion CAD Designs

## License

The project is distributed under the [MIT license](LICENSE).
