# vtab / container-move tests

Tree-shape tests for the vertical tabs patches and for moving containers
under `workspace_layout`. Each run starts a nested sway on the wlroots headless
backend, so nothing appears on screen and your running session is untouched.

```sh
nix develop    # provides python3 and foot, plus the build deps
meson setup build -Db_sanitize=address,undefined
ninja -C build
tests/vtab/run_tests.py --sway build/sway/sway
```

The sanitizer build is optional but recommended: memory errors fail the run
with a stack trace instead of crashing sway sometime later.

Every scenario runs once for each `workspace_layout` (vtabbed, tabbed,
stacking, default), with animations both off and on. Use `-k <name>` to run
matching scenarios only and `-v` to print each IPC command. When something
fails, the runner prints the expected and actual trees and keeps the sway log.

To add a scenario, write a `scenario_*` function in `run_tests.py` that drives
sway on workspaces of its own and returns the expected tree, and add it to
`SCENARIOS`.
