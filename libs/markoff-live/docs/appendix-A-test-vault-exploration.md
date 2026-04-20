# Appendix A: Test Vault Exploration

Agent report: exploration of existing test vaults at `/home/clinton/dev/Corbomite/testvaults/`.

---

## Directory Structure Summary

There are **3 main vaults** in the testvaults directory:

1. **obsidian-canvas-candy** — Canvas decoration and styling vault
2. **obsidian-hub-main** — Large community knowledge base (6,573 markdown files)
3. **starter-vault** — Personal knowledge management starter template (32 markdown files)

---

## VAULT 1: obsidian-canvas-candy

**Location:** `/home/clinton/dev/Corbomite/testvaults/obsidian-canvas-candy/`

### Purpose
A vault demonstrating Canvas feature extensions using CSS-only decorations (no JavaScript plugins required).

### Key Content
This vault documents **Obsidian Canvas features** with extensive examples:

**Index/Navigation Files:**
- `01 Features Index.md` — Lists canvas features (Borders, Cards, Headers/Labels, Stickers)
- `02 Stencil Index.md` — Stencil templates and shapes
- `03 Samples Index.md` — Real-world canvas samples
- `04 List of Decorations.md` — Comprehensive list of CSS class decorations

**Canvas Features Documented:**
- `Features/` directory with canvas feature demonstrations:
  - Borders.canvas — Border styling variations
  - Cards.canvas — Card customization
  - Headers and Labels.canvas — Text decoration
  - Stickers.canvas — Visual sticker elements

**Tutorial Content:**
- `Tutorial/Lesson 01 - Getting Started.canvas` through `Lesson 05 - Using Callout decorations.canvas`
  - Teaches canvas concepts, card decorations, callout integration
  - Demonstrates cssclass properties in canvas cards

**Sample Canvases (real-world examples):**
- Flowcharts (Accounts Receivable, Product Development, BPMN Travel Agency)
- Diagrams (Database/UML Class Diagrams, Venn Diagram)
- Organizational structures (Organizational Chart, Stakeholder Map)
- Data visualization (Apollo Missions Timeline, Home Network, Diabetes Research)
- Mind maps and Wardley maps
- Storyboards

**CSS Decorations Library** (in `.obsidian/snippets/canvas-candy.css`):
Key decoration classes documented:
- `cc-border-*` (none, bottom, left, right, top, dashed, dotted, double, dropshadow, rounded, squared)
- `cc-card-*` (center, fill, nocolor, opaque, transparent, gradient variants 0-315deg)
- `cc-rotate-*` (text and card rotation: 45-360 degrees)
- `cc-shape-*` (circle, parallelogram, parallelogram-right)
- `cc-callout-*` (center, header, footer variants)
- `cc-image-*` (clip, cover)
- `cc-label-*` (left, right, with/without borders)

---

## VAULT 2: starter-vault

**Location:** `/home/clinton/dev/Corbomite/testvaults/starter-vault/PKM LM/`

### Purpose
Personal Knowledge Management learning vault with best practices, workflows, and feature documentation.

### Key Content Files

**Editor & Keyboard Features:**
- `Keyboard Hotkeys and The Command Pallette.md`
  - Covers Command Palette activation and usage (Cmd-P)
  - Hotkey customization via Settings > Hotkeys
  - Examples: Daily Notes navigation with custom hotkeys
  - Plugin command discovery through Command Palette
  - Advanced: Mouse button programming for app-specific shortcuts

**Markdown & Content Formatting:**
- `YAML & Dataview.md`
  - YAML frontmatter metadata in Edit Mode
  - Dataview plugin syntax for creating dynamic tables
  - Examples of filtering and sorting data by metadata

- `Math Examples.md`
  - Inline math: `$E = mc^2$` syntax (MathJax)
  - Display math (multi-line equations)
  - Matrix notation, Greek letters, calculus, quantum mechanics, statistics
  - Examples: Schrodinger's equation, Maxwell's equations, derivatives, integrals

- `Mermaid Examples.md`
  - Flowcharts (LR, TB directions)
  - Sequence diagrams (participants, interactions)
  - Class diagrams (attributes, methods, inheritance)
  - State diagrams (transitions)
  - Pie charts and Gantt charts
  - Usage: Triple backticks with `mermaid` code block

**Obsidian Features & Workflows:**
- `Obsidian Setup.md`
- `Using Templates in Obsidian.md`
- `Obsidian vs. Roam Research.md`
- `Using Obsidian on iOS.md`
- `Callback URLs in Obsidian.md` — URI scheme documentation

**Productivity & Workflow Content:**
- `Daily Questions in Obsidian.md`
- `Meeting Notes in Obsidian.md`
- `Journaling in Obsidian with QuickAdd.md`
- `Task Management.md`
- `Timeblocking in Obsidian.md`

**Other Features:**
- `Connecting Notes & Bidirectional Linking.md` — Wikilink syntax
- `The Power of the Local Graph.md`
- `Syncing and Embedding Tasks with Todoist.md` — Integration examples
- `Turning Obsidian into the Perfect Writing App.md`

---

## VAULT 3: obsidian-hub-main

**Location:** `/home/clinton/dev/Corbomite/testvaults/obsidian-hub-main/` (6,573 files)

### Purpose
Comprehensive community-driven Obsidian knowledge base with guides, plugins, themes, and workflows.

### Key Content Directories

**04 - Guides, Workflows, & Courses/Guides/** — Core documentation:

**Markdown & Syntax Documentation:**
- `Markdown Syntax.md` — **KEY FILE**
  - References CommonMark syntax
  - Obsidian custom syntax: Wikilinks, Embeds, Display text, Tags, Highlights, Strikethrough, Comments, MathJax, Callouts, Mermaid
  - Lesser-known features: Setext headings, Strict Line Breaks, Semantic Line Breaks, Loose lists, Table line breaks with `<br>`, Nested code blocks

**Editor & Theme Development:**
- `How to update your plugins and CSS for live preview.md` — **KEY FILE**
  - Live Preview Mode vs. Edit/Preview modes
  - CodeMirror 5 (CM5) vs CodeMirror 6 (CM6) transition
  - DOM differences between Editor (Source) and Live Preview
  - `.is-live-preview` CSS class for detecting modes

**Graph & Customization:**
- `Graph view customization.md` — **KEY FILE**
  - WebGL-based graph rendering
  - CSS color customization for graph nodes/links

**Other Guides:**
- `Best Practices and Tips for Theme Development.md`
- `How to Style Obsidian.md`
- `Obsidian Design System Community File.md`
- `Default Obsidian Theme Colors.md`
- `How to get started developing plugins.md`

**02 - Community Expansions/** — Plugin ecosystem (300+ plugins documented)

---

## Content Usefulness Summary

**For Understanding Editor UX:**
- Best: `starter-vault/PKM LM/Keyboard Hotkeys and The Command Pallette.md`
- Best: `obsidian-hub-main/.../How to update your plugins and CSS for live preview.md`
- Best: `obsidian-hub-main/.../Markdown Syntax.md`

**For Visual Features:**
- Best: `obsidian-canvas-candy/` (extensive canvas examples)
- Best: `obsidian-hub-main/.../Graph view customization.md`

**For Markdown Examples:**
- Best: `starter-vault/PKM LM/Math Examples.md`
- Best: `starter-vault/PKM LM/Mermaid Examples.md`
- Best: `obsidian-hub-main/.../Markdown Syntax.md`
