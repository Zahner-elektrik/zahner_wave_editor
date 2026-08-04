# Contributing to the Zahner Wave Editor

Thanks for your interest in improving the Wave Editor. Bug reports, feature
requests and pull requests are all welcome.

# ⚖️ Contribution licensing

Please read this section before opening a pull request - it is the one thing we
cannot be flexible about.

The Wave Editor is dual licensed (see the *License* section of the
[README](README.md)): it is released to
everyone under the GNU GPL v3, and Zahner-Elektrik additionally ships the very
same code inside its proprietary products (notably Zahner Lab) under the Zahner
Software License. That only works as long as Zahner-Elektrik holds the necessary
rights to *all* of the code. A contribution licensed to us under the GPL alone
could never be shipped in Zahner Lab, which would force the two distributions
apart - so we ask for a slightly broader grant instead.

**By submitting a contribution (a pull request, a patch, or a code suggestion in
an issue) you agree that:**

1. You license your contribution to everyone under the **GNU General Public
   License, version 3**, the same terms as the rest of this project; **and**
2. You additionally grant **Zahner-Elektrik Ingeborg Zahner-Schiller GmbH & Co. KG**
   a perpetual, worldwide, non-exclusive, irrevocable, royalty-free and
   sublicensable right to use, reproduce, modify, distribute and **relicense**
   your contribution under any terms, including the proprietary Zahner Software
   License; **and**
3. You are legally entitled to grant the above - the contribution is your own
   original work, or you have the necessary permission from its copyright holder
   (for example your employer), and it does not knowingly infringe anyone's
   rights.

You keep the copyright to your contribution. Point 2 is a license grant, not an
assignment: it lets us ship your work in both products, it does not take your
work away from you, and it does not narrow anyone's GPL rights under point 1.

No signature, form or bot is required - opening a pull request is your agreement.
If you cannot agree to point 2, please open an issue describing the change
instead; we are happy to implement it ourselves.

Third-party code (a vendored library, a snippet from another project) is a
different matter: it cannot be covered by your grant. Please do not paste it into
a pull request. Open an issue naming the source and its license instead, and we
will handle the vendoring under `third_party/`.

# 🐛 Reporting bugs

Open an issue in the
[issue tracker](https://github.com/Zahner-elektrik/zahner_wave_editor/issues) and
include:

- what you did, what you expected, and what happened instead
- the version from **Help → About** and your operating system
- the `.zwj` document that reproduces it, if you can share one

# 🔨 Development setup

```bash
git clone https://github.com/Zahner-elektrik/zahner_wave_editor.git
cd zahner_wave_editor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

You need CMake ≥ 3.20, Qt ≥ 6.4 (Core, Gui, Widgets, LinguistTools, Test) and a
C++20 compiler. muParser is vendored, so there is nothing else to fetch.

# 📐 Code style and conventions

- **Formatting** is not up for discussion - `.clang-format` in the repository
  root decides. Run `clang-format -i` on everything you touch.
- **English** for code, comments, commit messages and documentation. User-facing
  strings go through `tr()` and get translated in `translations/`.
- **Keep the core GUI-free.** `src/core/` may depend on `Qt6::Core` only. Anything
  needing widgets belongs in `src/ui/`. This is what makes the model unit-testable,
  so please do not erode it.
- **Every source file carries an SPDX header:**

  ```cpp
  // SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
  // SPDX-License-Identifier: GPL-3.0-only
  ```

  New files you author may name you in the copyright line as well.
- **Comments explain why, not what.** The existing headers in `src/core/` are the
  reference for the level of detail we aim at - they document the contract
  (rejected inputs, edge cases, units), not the implementation.

# ✅ Tests

`tests/` holds QtTest-based unit and widget tests, wired into `ctest`.

- Every bug fix gets a test that fails before it and passes after.
- New core behavior (segment types, sampling, IO, validation) needs unit tests.
  Core code is expected to stay fully covered.
- New UI behavior is testable too - see `tst_mainwindow.cpp` and
  `tst_propertypanel.cpp` for how widgets are driven from tests.
- `ctest --test-dir build --output-on-failure` must be green before you open the
  pull request. Widget tests already run with `QT_QPA_PLATFORM=offscreen`, set per
  test in `tests/CMakeLists.txt` - a new widget test needs the same line, or it
  will fail on a headless CI machine.

# 🌍 Adding a translation

Copy `translations/zwe_de.ts` to `translations/zwe_<locale>.ts`, add it to the
`qt_add_translations` call in `CMakeLists.txt`, and translate with Qt Linguist.
Partial translations are fine - untranslated strings fall back to English.

Also add the language to `ShippedLanguages` in `src/ui/appsettings.cpp`, with its
endonym - that list is what **Edit → Settings** offers, and what the system
language is matched against. If you translate `resources/help/` as well, put the
pages in a directory named after the same language code; the help browser falls
back to English per page.

# 🧩 Adding a segment generator

The most requested kind of contribution, and a well-trodden path through the code:

1. `src/core/segment.h` - a `...Params` struct, an entry in `SegmentType`, and the
   struct added to the `SegmentParams` variant
2. `src/core/segment.cpp` - `segmentType()` and `defaultSegment()`
3. `src/core/sampling.cpp` - evaluate it in `segmentValueAt()`
4. `src/core/zwjio.cpp` - read and write the parameters, with validation and a
   clear error message naming the offending JSON path
5. `src/core/segmentcontinuity.cpp` - decide whether the new type should adopt its
   neighbors' values when inserted
6. `src/ui/propertypanel.cpp` - the editing widgets
7. `resources/icons/toolbar/wave-<name>.svg` plus the `.qrc` entry, and the action
   in `src/ui/mainwindow.cpp`
8. `tests/tst_model.cpp`, `tests/tst_sampling.cpp`, `tests/tst_zwjio.cpp` - tests
   for the value, the defaults, and the file-format round trip
9. `resources/help/*/index.html` - a line in the built-in help

Bumping the `.zwj` format version is only necessary when an **existing** document
would no longer load; adding a new segment type is a forward-compatible change.

# 🔀 Pull requests

- One topic per pull request, rebased on `main`, with a description of the *why*.
- Say how you tested it, and mention the platforms you built on.
- Screenshots or a short recording help a lot for anything visible.

# 📧 Questions

Ask in an issue, or mail
[support@zahner.de](mailto:support@zahner.de?subject=Zahner%20Wave%20Editor%20Contribution).
