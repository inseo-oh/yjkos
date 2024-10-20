# Cross compiler build

Cross compiler build is done by simply running Make on the project root(e.g. `gmake`). It should do everything you need, and if cross compiler build dies in the middle, it should pick up from where it died.

However, there might be cases where you want to rebuild certain parts of toolchain. You control it using something called "marker files".

## Marker files

Marker files are stored to `toolchain/markers-<arch>` directory(e.g. `toolchain/markers-i586`), and whenever toolchain build script finishes one step, it creates marker file here to remember that it doesn't need to redo it next time. So to force script to redo certain parts of the build process, simply delete the corresponding marker file.

Name of each file follows `<package>.<step>` format. For example, `gcc.configure` file means configure step of gcc.

Note that usually deleting a marker file will also cause any steps for that package after that point to be re-run. For example, if you re-run the configure step, it will also re-run compile and install step since each step depends on the previous step.
