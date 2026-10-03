#pragma once
// MP fork (mod enforcement B, 2026-10-03): the commit this engine was built from — the "build" identity a co-op client
// sends the server at join. CI overwrites this file before each build (r0-dedicated.yml "Stamp co-op build commit");
// a local build reports "local", and a local build therefore matches only another local build.
#define COOP_BUILD_COMMIT "local"
