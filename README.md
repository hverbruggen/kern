# kern

I find myself doing a lot of work with AI agents, and often the communication
happens via Markdown files. Editing these in a text editor is a bit plain, so I
thought I could do a bit better. Kern is a lightweight Linux editor for
Markdown and plain text. Markdown renders as you type, with a very nice
interface to create, edit and modify tables.

## Features

- Visual mode edits Markdown source with formatted headings, code, equations, and tables. Syntax appears locally when the caret enters a construct.
- Or, if you're a purist, you can switch to source view for the raw text.
- Click into a table cell to edit it and arrow keys and Tab to move between
cells.
- Open several files in tabs and browse a project folder in the sidebar. The
  clock button next to Open lists your ten most recent project folders.
- Right-click a file in the sidebar to open it in its default application or
  choose a different one.
- Plain text and standalone LaTeX files edit as literal source. Equations embedded
  in Markdown have native previews in Visual mode.
- Undo and redo, atomic saves, and a warning when a file changes on disk.
- Follows the current [Omarchy](https://omarchy.org) colors and font when you're using that OS. Otherwise it uses a light default theme.

## Install

You need Linux, Qt 6 with Quick Controls, a Rust toolchain
(1.80 or newer), CMake, Ninja, a C++17 compiler, tinyxml2 development files, and GIO's `gio` command for
opening unsupported files with their default application. CMake downloads a
pinned, hash-verified MicroTeX release for native equation rendering. Cargo
dependencies are recorded in the lockfiles. Build and compiler temporary files
should live outside synced source directories; set `TMPDIR` as well as the build
location when using external storage.

```
cmake -S ui-qt -B build -G Ninja
cmake --build build
./build/kern notes.md
```

To install the program, icon, and desktop entry into `~/.local`, which also lets
you pick kern in "Open with":

```
cmake --install build --prefix ~/.local
```

`kern FILE` opens a file and `kern FOLDER` opens a project. With no argument
kern starts with a new empty document, ready to type in. Opening a file replaces
it if you haven't typed anything. A file opened without a project, as from a
file manager, starts with the file list collapsed; Ctrl+Shift+B shows it.

## Using kern

It's very keyboard centric, but can be used well with a mouse using icons along
the top. Hover over any icon to see what it does and its shortcut.

| Action | Shortcut |
| --- | --- |
| New document | Ctrl+N |
| Open a project folder | Ctrl+O |
| Save, Save as | Ctrl+S, Ctrl+Shift+S |
| Reload from disk | Ctrl+R |
| Close tab, quit | Ctrl+W, Ctrl+Q |
| Undo, redo | Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z |
| Bold, italic | Ctrl+B, Ctrl+I |
| Bulleted, numbered list | Ctrl+Shift+8, Ctrl+Shift+7 |
| Insert link or image | Ctrl+Alt+K, Ctrl+Alt+I |
| Insert table, then enter rows and columns | Ctrl+Alt+T |
| Table: next cell, or add a row from the last cell | Tab |
| Table: add row above or below | Ctrl+Alt+Up, Ctrl+Alt+Down |
| Table: add column left or right | Ctrl+Alt+Left, Ctrl+Alt+Right |
| Table: delete row or column | Ctrl+Alt+Shift+Up, Ctrl+Alt+Shift+Left |
| Show or hide the file list | Ctrl+Shift+B |
| Focus the file list | Ctrl+Shift+E |
| Zoom (30% to 200%) | Ctrl+plus, Ctrl+minus, Ctrl+0, or Ctrl+wheel |

Arrow keys move into and out of tables. Down on the last line above a table
lands at the start of its first cell, and Up on the first line below a table
lands at the start of the last row's first cell. Shift+Down or Shift+Up there
selects the whole table instead, as does dragging the mouse from the text on
one side across the table. Delete or Backspace then removes the table.

Click below the last block to type at the end of the document.

## Things to know

- Click formatted text to reveal its Markdown syntax. Copying text includes the Markdown markers.
- Paragraphs wrap to the window width. Kern preserves source line breaks and indentation when saving.
- Inline code has a shaded background; code blocks have syntax colouring.
- Use `$...$` for inline equations and `$$...$$` for display equations. Fractions, matrices, and aligned equations render in place. Click an equation to edit its TeX.
- HTML stays editable as source. Table cells also edit Markdown; pasted line breaks become spaces.
- Local images appear in Visual mode. Click an image to edit its Markdown.
- Hover over a link for actions, or Ctrl-click to open it. Click its label to edit the destination.
- Save with Ctrl+S. Kern asks before discarding unsaved changes and warns if the file changes on disk.

## Development

```
cargo test -p kern-core                         # table parser and editor

cmake --build build --target kern-checks        # builds the UI checks

ui-qt/tests/run-checks.sh build                 # runs them without a display
```
- `core/` is the Markdown table parser and editor, written in Rust.
- `ui-qt/bridge/` exposes `core/` to C++.
- `ui-qt/` is the Qt Quick application: `main.qml` is the interface, `
  controller.cpp` holds documents and edits, `theme.cpp` follows the system
  theme.
- `ui-qt/tests/` holds the UI checks and fixtures. The checks drive the real
  interface offscreen, so they cover logic and layout but not real keyboard,
  mouse, or input-method behavior.

## License

MIT. See `LICENSE`. 

The application icon's lowercase "k" is derived from the "Great Vibes" typeface
by TypeSETit, used under the SIL Open Font License 1.1. The toolbar icons are a
subset of JetBrainsMono Nerd Font, also under the OFL; see `ui-qt/fonts/`.

## Editor architecture

The interaction model of the editor is heavily influenced by that of
[Zettlr](https://github.com/Zettlr/Zettlr/tree/93d9470114ecf4ed663a346ba099d9e11e37fd2f/source/common/modules/markdown-editor/renderers), one of my favorite writing tools.
Kern inherits much of the concepts, but uses a native Qt implementation.
