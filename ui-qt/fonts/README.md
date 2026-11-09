# Bundled icon font

`kern-icons.ttf` holds the icon glyphs used in the toolbar, file lists, and table buttons, subset from JetBrainsMono Nerd
Font (SIL Open Font License 1.1, https://openfontlicense.org) and renamed
"Kern Icons". The glyphs come from Font Awesome and VS Code Codicons through
Nerd Fonts. kern loads the file at startup, so icons render whatever font the
theme uses.

`kern-hidden.ttf` and `kern-spacer.ttf` are original empty layout fonts under
Kern's MIT license. They let the source editor conceal syntax or reserve room
for a preview without replacing document characters. Regenerate them with
`generate-layout-fonts.py` (requires Python fonttools).
