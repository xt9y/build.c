# Additional build capabilities

The build description remains a C file using `<cbuild.h>`. No second
configuration language or runtime is required.

## Inspect without building

```sh
c check              # validate targets, source patterns and dependency cycles
c tree               # print target and dependency relationships
c compdb             # write compile_commands.json without compiling
c compdb app         # inspect only one target
```

## Dependencies

```c
C_Dependency *local = c_path(b, "local", "../local-library");
c_dep_header_only(local);
c_use(app, local);

C_Dependency *system = c_pkg_config(b, "sqlite", "sqlite3");
c_use(app, system);

C_Dependency *library = c_registry(b, "xxhash", "xxhash", "0.8.4");
c_dep_source(library);
c_dep_sources(library, "xxhash.c");
c_use(app, library);
```

`c_path` uses files directly, never updates `c.lock`, and supports source
libraries and header-only projects. `c_pkg_config` resolves flags with the
installed `pkg-config` executable on macOS/Linux; native Windows currently
reports an explicit unsupported-dependency error.

`c_registry` uses `C_REGISTRY_URL` or the Smelt registry by default.
It resolves `packages/<package>/<version>/source.smelt`, locks the registry
revision and the upstream source commit in `c.lock`, and then uses the
existing dependency build APIs. Registry `overlay.smelt` build rules are
**not** executed: express compilation settings in `build.c` instead.

## Clang C++ modules

Add named module interface sources to the target and compile with C++20:

```c
c_sources(app, "src/*.cppm");
c_sources(app, "src/*.cpp");
c_flag(app, "-std=c++20");
```

The native macOS/Linux backend recognizes exported module declarations in
`.cppm` files, checks for duplicate providers and import cycles, and
precompiles dependencies into cached `.pcm` files before compiling objects.
It disables unity compilation for module targets. The module toolchain uses
`CXX` when set, otherwise `clang++`; it requires Clang-compatible module
flags. Modules imported from other targets, header units, module partitions,
and arbitrary source extensions are not fully supported.

For the optional libc++ C++23 standard library module:

```c
c_std_module(app, NULL);            /* search known libc++ std.cppm paths */
c_std_module(app, "/path/std.cppm"); /* or set an explicit source path */
```

Set `C_STD_MODULE_SOURCE` to override automatic lookup. The standard library
module needs an installed Clang/libc++ toolchain that provides `std.cppm`.

Ordinary C/C++ targets without modules use the existing compilation path.
