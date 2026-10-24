# Embedded PDF bookmarks

This fork uses PDF outline entries for the main **Add Bookmark** action.
The action, including the page context menu and keyboard shortcut, asks for a
title and appends a top-level bookmark pointing to the selected page. The former
**Contents** panel is labelled **Bookmarks**. The local bookmark panel is hidden.
Previous/next navigation and the Bookmarks menu use the document outline.

New entries are written inside the PDF with **Save** or **Save As**. Existing
outline entries, nested children, destinations, actions and formatting are
preserved. Right-click a bookmark and choose **Remove Bookmark** to remove
it and its children. Drag between entries to reorder them, onto an entry to
make it a child, or into the blank area to return it to the top level. A parent
moves together with its descendants. All edits participate in undo/redo,
including across a save. Use **Save** to persist the structure in the PDF.
Renaming existing entries is not implemented yet.

Original outline metadata is retained in a private temporary file for the
current session so undo can restore deleted bookmarks even after saving.
The temporary file is removed when the document is closed.

## Building

The PDF generator now requires the **QPDF C++ development library, version 11 or
newer**, in addition to the existing Qt 6, KDE Frameworks 6 and Poppler Qt 6
dependencies. CMake discovers its `qpdf` package and links `qpdf::libqpdf`.
QPDF is used only when saving document bookmark edits; ordinary rendering and
annotation editing continue through Poppler.

Use the existing build instructions in `README.md`, then run:

```sh
ctest --test-dir build -R '^(pdfBookmarksTest|documenttest)$' --output-on-failure
```

The existing `pdfBookmarksTest` covers persistence, hierarchy, Unicode titles, named and URI
destinations, preservation of page streams and annotations, encrypted files,
repeated saves, undo/redo after saving, and invalid input. The `documenttest`
regression exercises Okular's undo history and generator backing-file swaps.
