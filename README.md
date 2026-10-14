# kern

I find myself doing a lot of work with AI agents, and often the communication
happens via Markdown files. Editing these in a text editor is a bit plain, so I
thought I could do a bit better. Kern is a lightweight Linux editor for
Markdown and plain text. Markdown renders as you type, with a very nice
interface to create, edit and modify tables.

## Features

- You're editing in a rendered view, so pretty text with headers showing as
  headers and tables in layout.
- Or, if you're a purist, you can switch to source view for the raw text.
- Click into a table cell to edit it and arrow keys and Tab to move between
  cells.
- Open several files in tabs and browse a project folder in the sidebar.
- Plain text files edit as plain text. LaTeX files show a read-only preview and
  are edited in Source.
- Undo and redo, atomic saves, and a warning when a file changes on disk.
- Follows the current [Omarchy](https://omarchy.org) colors and font when
  you're using that OS. Otherwise it uses a light default theme.

## Install

You need Linux, Qt 6 with Quick Controls, a Rust toolchain
(1.80 or newer), CMake, Ninja, and a C++17 compiler.

`cmake -S ui-qt -B build -G Ninja`

`cmake --build build`

`./build/kern notes.md`

To install the program, icon, and desktop entry into `~/.local`, which also lets
you pick kern in "Open with":

`cmake --install build --prefix ~/.local`

`kern FILE` opens a file and `kern FOLDER` opens a project. With no argument
kern starts empty. A file opened without a project, as from a file manager,
starts with the file list collapsed; Ctrl+Shift+B shows it.

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

- **Edits in "Visual" mode can reformat the paragraph you edit.** Qt's Markdown
  exporter normalizes what it rewrites, so some escapes and spacing in that
  paragraph may change. kern leaves paragraphs you did not touch exactly as
  they were, and keeps inline HTML tags in the paragraph you edit. The Visual
  view does not render HTML formatting such as `\<span style>`; the tags stay in
  the file. Use Source when you need exact syntax.
- **Tables are one line per row.** Merged cells and multi-line cells are not
  supported. A cell cannot contain a line break.
- **Remote images are not loaded.** A document cannot make kern contact the
  network. Local images display.
- **Links open only if considered low-risk.** kern opens web and mail links, and
  local files that exist and are not executable. Links to headings inside a
  document do nothing.
- **Preview rendering is Qt's.** It can differ from GitHub or other renderers.
  Saving always writes the source text, not the preview.
- **kern handles UTF-8 text.** It shows a message instead of opening binary or
  non-UTF-8 files.
- **There is no autosave.** kern asks before discarding unsaved changes, but a
  crash loses them.

## Development

`cargo test -p kern-core                                  # table parser and
editor`

`cmake --build build --target kern-checks                 # builds the UI checks`

`ui-qt/tests/run-checks.sh build                          # runs them without a
display`

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
