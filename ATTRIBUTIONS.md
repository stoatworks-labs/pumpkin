# Attributions

Pumpkin is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is a provisional hand copy. Once Pumpkin is registered, the master lists live in the
`stoatworks-backend` repo and are pushed out by `scripts/sync-attributions.py`, and this
file is generated: edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The pumpkin patch — Stoatworks Test Card

<https://github.com/stoatworks-labs/test-card>  
Licence: MIT  
Copyright: Stoatworks Labs

The jack-o'-lantern, its six faces and their expressions (`src/patterns/art/halloween.ts`), the candle and expression timing, which face goes on which tile and each tile's night (`src/patterns/halloween.ts`), and the seeded hash and generator (`src/patterns/random.ts`) are ported line for line into source/Lantern.cpp and source/Random.h. `pktest --port` holds the port to numbers the test card's own TypeScript prints, and `pktest --testcard` holds the picture to pictures its own painter draws.

### Parameter table, GL state, clock and harness — Stoatworks langton

<https://github.com/stoatworks-labs/langton>  
Licence: MIT  
Copyright: Stoatworks Labs

HostOrder() with the About block last, the host-clock unit vote, GLState.h, Diag, the harness's context, PNG writer, cue sheets and --pipe, --negative, --bench, tools/verify.sh, tools/mutate.sh, tools/glslc.sh and tools/sweep.py are langton's (which carries them from radar, conway, boreal and tinsel).

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something this plugin calls directly — listed because it is present in the checkout.
