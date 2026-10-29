# planetbake

The program is built into `bin/` (`planetbake`, or `planetbake.exe` on Windows) by the game's build. It is not a KosmosLink addon, so there is no `addon.json`; it sits here next to KosmosMap so it ships with the game.

Run it with no options for the preview window (Linux, where GTK 3 was found when it was built), or with options for the command line: `bin/planetbake --help`. It uses at most 4 threads.

Source and full documentation: `sdk/planetbake/` and its `README.md`.
