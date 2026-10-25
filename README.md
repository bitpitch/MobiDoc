# MobiDoc – PDF Document Viewer

MobiDoc is an open-source document viewer based on KDE Okular.

It supports PDF, PostScript, Comic Book, and various image formats, including native PDF annotations.

MobiDoc extends the original document viewer with advanced PDF bookmark management, allowing users to create, edit, rename, delete, and organize hierarchical bookmarks directly within PDF documents.

All bookmark modifications can be saved directly into the PDF file.

### Downloads

Installation packages and releases will be available in the MobiDoc repository.

### User manual

Documentation for MobiDoc is currently under development.

### Bugs

Please report bugs and feature requests through the MobiDoc repository.

### Source code

The MobiDoc repository contains the source code for:

* the desktop document viewer,
* document rendering backends,
* PDF annotation support,
* hierarchical PDF bookmark management,
* bookmark editing with undo and redo support.

### PDF Bookmark Management

MobiDoc provides the following features:

* Create, rename, and delete PDF bookmarks.
* Reorder bookmarks using drag and drop.
* Create and manage hierarchical bookmark structures.
* Move bookmarks between hierarchy levels.
* Undo and redo bookmark modifications.
* Save bookmarks directly into PDF documents.

## Contributing

MobiDoc is an independent open-source project.

Contributions, bug fixes, and improvements are welcome.

### Build instructions

MobiDoc uses CMake and KDE Frameworks.

After obtaining the source code, build the application using:

```bash
cmake -S . -B build
cmake --build build
```

To install:

```bash
cmake --install build
```

Additional dependencies may be required depending on the operating system.

### clang-format

MobiDoc uses clang-format to maintain consistent source code formatting.

## Credits

MobiDoc is based on Okular, originally developed by the KDE community.

The project incorporates modifications and additional functionality for PDF bookmark management.

All original copyright notices and author attributions are preserved.

MobiDoc is an independent project and is not officially affiliated with KDE.

## License

MobiDoc is distributed under the applicable open-source licenses of its components.

Original copyright notices, license files, and attribution requirements are retained in the source code.

For complete licensing information, refer to the license files included in the repository.
