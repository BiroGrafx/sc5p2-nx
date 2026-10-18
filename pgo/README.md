# PGO profile

The profile-guided optimisation profile of this code: one console session played on the instrumented NRO of
2026-10-12 (the three CPU rounds; the profile of the released 1.0.0 is in the `v1.0.0` tag), merged with
`tools/win/pgo_merge.ps1`. Each `.gcda` file holds the branch and call counts of one
object of the build. They are counts only: no game code or data.

`tools/win/build_switch.ps1 -Pgo use` builds with it (the build reads this folder: `SA2_PGO_DIR` in
`app/CMakeLists.txt`). How to record a new one is in [docs/building.md](../docs/building.md#profile-guided-optimisation).

Each file is named by its object's path inside the build folder (`out\nx`), with `#` for each folder separator, as GCC
names them with `-fprofile-prefix-path`. So the profile applies whatever folder you build in, with two exceptions:
the four SDK files compiled into the app itself (their object paths contain the full path of the checkout,
`D:\sonic adventure 2\sa2-nx`) and the functions local to one file (GCC identifies those by their file's full path).
Everything else, the game's translated code above all, keeps its profile.

The profile matches the code of this repository: a function changed since then loses its part of it, and the rest
still applies.
