# Omasign

A dead-simple PDF viewer and signer for Omarchy, built with Qt Quick, C++ and poppler.
Open a PDF, drop a saved signature or a line of text anywhere on a page, save.
Signatures and text are written as PDF annotations with embedded appearances,
so every viewer shows them.

## Build

    ./bin/build        # produces build/omasign
    ./bin/test         # headless unit + UI tests
    cd pkgbuild && makepkg -si

Requires `qt6-base`, `qt6-declarative`, `poppler-qt6` and `xdg-desktop-portal`.

## Use

- **Open** (Ctrl+O), or drop a PDF on the window, or `omasign file.pdf`.
- **Text** (Ctrl+T) drops a text box on the visible page. Type, then click away.
  Double-click a box to edit it again.
- **Sign** lists your saved signatures. *Draw signature…* opens a pad for the mouse or
  trackpad; *Import image…* takes a photo or scan and makes the paper transparent.
  Click a signature to drop it on the page, or the × next to it to forget it.
- Drag anything to move it, drag the corner handle to resize. Delete removes the selected item.
- **Save** (Ctrl+S) writes into the file. Ctrl+Shift+S saves a copy.

Signatures live in `~/.local/share/omasign/signatures/` as PNG files.
