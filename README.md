# kern

I find myself doing a lot of work with AI agents, and often the communication
happens via Markdown files. Editing these in a text editor is a bit plain, so I
thought I could do a bit better. Kern is a lightweight Linux editor for
Markdown and plain text. Markdown renders as you type, with a very nice
interface to create, edit and modify tables.

## Features

- Visual mode edits Markdown source with formatted headings, code, equations,
  and tables. Syntax appears locally when the caret enters a construct.
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
- Follows the current [Omarchy](https://omarchy.org) colors and font when
  you're using that OS. Otherwise it uses a light default theme.

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
| Insert link, image, table | Ctrl+Alt+K, Ctrl+Alt+I, Ctrl+Alt+T |
| Table: add row above or below | Ctrl+Alt+Up, Ctrl+Alt+Down |
| Table: add column left or right | Ctrl+Alt+Left, Ctrl+Alt+Right |
| Table: delete row or column | Ctrl+Alt+Shift+Up, Ctrl+Alt+Shift+Left |
| Show or hide the file list | Ctrl+Shift+B |
| Focus the file list | Ctrl+Shift+E |
| Zoom (30% to 200%) | Ctrl+plus, Ctrl+minus, Ctrl+0, or Ctrl+wheel |

Dialogs work from the keyboard. The default button has focus, so Enter confirms
it. Left and Right move between buttons and Esc cancels. When you close a tab
with unsaved changes, the default is to discard them.

The folder and file dialogs (open folder, new file, save as, choose image) are
keyboard-first too. Up and Down select, Enter goes into a folder or picks a
file, Left or Backspace goes up a level, and typing jumps to a name. Tab moves
to the action button, Enter confirms, and Ctrl+L edits the path.

In the file list, Up and Down move, Shift+Up or Shift+Down selects a range, and
Enter opens the selected files or opens and closes a folder. Right and Left
expand and collapse folders, and Esc returns to the document. After you open a
project folder, the file list has the focus.

Arrow keys move into and out of tables. Down on the last line above a table
lands at the start of its first cell, and Up on the first line below a table
lands at the start of the last row's first cell. Shift+Down or Shift+Up there
selects the whole table instead, as does dragging the mouse from the text on
one side across the table. Delete or Backspace then removes the table, and
Undo brings it back.

To insert a table from the keyboard, press Ctrl+Alt+T, type the number of rows,
Tab, type the number of columns, Tab, and Enter. The caret lands in the first
cell. Tab from the last table cell adds a row. Click below the last block to start
typing at the end of the document.

## Things to know

- **Visual edits the source directly.** Markdown delimiters are hidden outside
  the caret or selection and revealed locally while editing. Hover does not
  reveal syntax. Ordinary source newlines flow as spaces in Visual mode, so
  paragraphs wrap to the window width. Blank lines, explicit Markdown hard breaks,
  code blocks, and raw HTML retain their breaks. The original source newlines are
  preserved when editing and saving. Typing or
  deleting backticks, emphasis, headings, links, or fences updates the formatting.
  Copying a selection copies its source, including Markdown markers.
- **Code stays distinguishable.** Inline code has a subtle background; fenced
  and indented code have panels. Fences and language labels reappear while editing
  the block. Code colouring currently uses a basic token highlighter.
- **Equations have native previews.** MicroTeX draws inline and display math in
  Qt, including fractions, roots, scripts, matrices, aligned equations, cases,
  and scalable delimiters. Click a preview to select its TeX source; moving away
  restores the preview. Invalid or unsupported equations stay visible as source
  with a diagnostic on hover. This is a mathematical TeX subset, not full LaTeX
  or exact KaTeX compatibility: document commands, macro definitions, and `mhchem`
  are not supported. No external TeX process runs. Standalone `.tex` files remain
  source editors. Markdown math uses `$...$` and `$$...$$` delimiters.
- **Raw HTML stays source.** Tags, comments, HTML tables, and iframes are never
  rendered. HTML attributes and comments remain editable. Markdown-looking text
  inside an HTML block is not turned into a table widget.
- **Tables edit source within each cell.** Merged cells and multi-line cells are
  not supported. Pasted line breaks become spaces; existing `<br>` tags remain
  literal source. Single-cell edits preserve the rest of the table's source.
- **Images load locally.** Markdown images can show local previews; remote images
  remain source. Clicking an image reveals its Markdown.
- **Links have source and display ranges.** Labels are shown outside editing;
  clicking into them reveals the destination. Hover offers link actions;
  Ctrl-click opens links accepted by the existing URL checks.
- **Unsupported extensions remain editable source.** Kern does not implement all
  Zettlr/Pandoc features, such as citation resolution, Mermaid diagrams, or Pandoc
  div widgets. Rendering never deletes or rewrites those constructs.
- **kern handles UTF-8 text.** Binary or non-UTF-8 files show a message with an
  option to open them in the default application for their file type.
- **There is no autosave.** kern asks before discarding unsaved changes, but a
  crash loses them.

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
