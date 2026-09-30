# Qt accessibility: upstream issue drafts (NOT FILED)

**Status: drafted, not filed - awaiting user decision.** Filing is
outward-facing (bugreports.qt.io is public and indexed); nothing here has
been posted anywhere. Written during G1 task A5.2 (2026-09-29).

Facts below were checked against the installed Qt 6.11.2
(`/usr/include/qt6/QtGui/qaccessible_base.h`, `libQt6Gui.so.6`) and a qtbase
checkout at `/home/clinton/src/qtbase` (v6.12.0-beta1-1483, module version
6.13.0 alpha, `src/gui/accessible/`, `src/gui/accessible/linux/`), plus
`/usr/include/at-spi-2.0/atspi/atspi-constants.h`.

## Correction to our own spec (not an upstream issue)

Spec `2026-08-19-g1-canvas-accessibility-design.md` §4.2/§4.6 finding 4 says
`ROLE_BLOCK_QUOTE` is unreachable from Qt. **That is wrong for Qt >= 6.9:**
`QAccessible::BlockQuote` (0x431, "since 6.9", qtbase commit `0b5874bc96f`
"a11y: Add new BlockQuote role") maps to `ATSPI_ROLE_BLOCK_QUOTE` in
`qspiaccessiblebridge.cpp`. The canvas now uses it (A5.2). Nothing to file
for block quotes. The level attribute (`Attribute::Level` -> AT-SPI `level`
object attribute) also works (A1.0 probe; `atspiadaptor.cpp` converts it) -
nothing to file.

## Draft 1: no accessible role for math content on AT-SPI

**Title:** QAccessible: no role maps to ATSPI_ROLE_MATH

**Version:** Qt 6.11.2 (also present in qtbase dev), Linux/AT-SPI.

**Summary:** `atspi-constants.h` defines `ATSPI_ROLE_MATH` (also
`_MATH_FRACTION`, `_MATH_ROOT`), but no `QAccessible::Role` maps to it in
`QSpiAccessibleBridge`'s role table. The closest role, `QAccessible::Equation`
(0x37), maps to `ATSPI_ROLE_TEXT` (localized name "equation"). Applications
that render math (e.g. a Markdown editor with `$$...$$` blocks) can only pick
`StaticText` (-> `ROLE_LABEL`) or `Equation` (-> `ROLE_TEXT`), neither of
which lets Orca announce "math".

**Repro:** expose an accessible with role `QAccessible::Equation`; read its
role over `org.a11y.atspi.Accessible.GetRole` (returns ROLE_TEXT).

**Suggestion:** map `QAccessible::Equation` to `ATSPI_ROLE_MATH` (a behavior
change - may prefer a new role or an opt-in), or add a dedicated role.

## Draft 2: no accessible role for footnotes

**Title:** QAccessible: no Footnote role (IA2_ROLE_FOOTNOTE / ATSPI_ROLE_FOOTNOTE)

**Summary:** `qaccessible_base.h` lists `// IA2_ROLE_FOOTNOTE = 0x40F,` as a
commented-out placeholder between `Footer` and `Form`; `ATSPI_ROLE_FOOTNOTE`
exists in at-spi2-core. Document-like widgets that expose footnote/endnote
definitions have to fall back to `Section`.

**Suggestion:** add `QAccessible::Footnote = 0x40F` mapped to
`ATSPI_ROLE_FOOTNOTE` (IA2 value on Windows), following the pattern of the
6.9 `BlockQuote` addition.

## Draft 3: no standard expand/collapse action names

**Title:** QAccessibleActionInterface: no expand/collapse action names

**Summary:** the standard names are `pressAction`, `increase/decreaseAction`,
`showMenuAction`, `setFocusAction`, `showOnScreenAction`, `toggleAction`,
and the scroll/page actions (`qaccessible.h`). `atspiadaptor.cpp`
(`effectiveActionNames`) forwards action names verbatim as the
`org.a11y.atspi.Action` name. A foldable section (heading fold, disclosure)
can only expose `toggleAction()`, which does not say which direction the
toggle goes; direction is only conveyed by the `expanded`/`collapsed` state
flags and the localized description.

**Suggestion:** add `expandAction()`/`collapseAction()` (AT-SPI clients and
IA2 already know "expand"/"collapse" action names) and have Qt's tree/section
widgets use them.

## Decision log

- 2026-09-29: drafted, not filed - awaiting user decision. If filed, check
  Draft 1 first against Qt's current `dev` role table (it changes often).
