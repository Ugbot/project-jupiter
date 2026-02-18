# Tracy Integration Plan

## Status (current milestone)
- [x] Vendored Tracy as a git submodule at `vendored/tracy`
- [x] CMake option `JUPITER_ENABLE_TRACY` (default ON) with static Tracy client
- [x] Engine-level profiling shim (`profiling/`) exposing Tracy macros and helpers
- [x] Baseline instrumentation in `core` lifecycle to validate linkage

## Next steps (engine integration)
- [ ] Add thread naming + frame markers around main loop tick in each sample in `projects/`
- [ ] Wire Vulkan GPU zones (command buffer scopes and queue submissions) using `TracyVulkan.hpp`
- [ ] Expose memory allocation callbacks from `memory` module to Tracy alloc/free events
- [ ] Instrument ECS and event dispatch hot paths with scoped zones and plots (no allocations in hot path)
- [ ] Provide a minimal headless-friendly Tracy server launcher script/config (no GUI dependency in CI)
- [ ] Document capture workflow and profiling conventions in `docs/` for contributors

## Build & usage notes
- Tracy builds as a static library and is linked via the `profiling` interface target.
- On-demand capture is enabled; connect with the Tracy UI to start recording.
- Network exposure is limited to localhost by default to keep captures local to the dev machine.







