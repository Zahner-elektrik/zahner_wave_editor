# Vendored third-party dependencies

| Dependency | License | Notes |
|---|---|---|
| muParser 2.3.5 | BSD-2-Clause | Vendored under `third_party/muparser/` from the [v2.3.5 release](https://github.com/beltoforion/muparser/releases/tag/v2.3.5) tarball: `include/` and `src/` plus `LICENSE`. Upstream's `samples/`, `test/`, `muParserInt` (integer-parser variant) and `muParserDLL` (C DLL API) are unused here and not vendored. Built as a static lib via our own `third_party/muparser/CMakeLists.txt` (`MUPARSER_STATIC` defined, no OpenMP, no samples/tests). |

Keep every vendored dependency's license file next to its sources.
