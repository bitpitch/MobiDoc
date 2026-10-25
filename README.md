# MobiDoc – PDF Document Viewer

MobiDoc is an open-source document viewer based on KDE Okular.

It supports multiple document formats, including PDF, PostScript, Comic Book, and various image formats, with native PDF annotations.

MobiDoc extends Okular with enhanced PDF bookmark management, allowing users to create, edit, rename, delete, and organize hierarchical bookmarks directly within PDF documents.

Bookmark changes can be saved in the PDF itself, making them portable across compatible PDF viewers.

### Downloads

MobiDoc is currently under development.

Installation packages and release information will be published in the MobiDoc repository.

### User manual

MobiDoc is based on Okular and shares many of its features.

The original Okular documentation is available at:

https://docs.kde.org/?application=okular&branch=stable5

### Bugs

Please report issues related to MobiDoc through the MobiDoc repository.

For issues affecting the original Okular project, refer to:

https://bugs.kde.org/buglist.cgi?product=okular

### Source code

MobiDoc is a fork of KDE Okular.

Original Okular repository:

https://invent.kde.org/graphics/okular.git

The source code includes:

* the desktop document viewer application,
* the `okularpart` KParts plugin,
* the `okularkirigami` mobile application,
* several `okularGenerator_xyz` plugins providing backends for different document types,
* additional PDF bookmark management functionality.

### PDF Bookmark Management

MobiDoc provides extended bookmark editing capabilities:

* Create new PDF bookmarks.
* Rename existing bookmarks.
* Delete bookmarks.
* Reorder bookmarks using drag and drop.
* Organize bookmarks into hierarchical structures.
* Move bookmarks between hierarchy levels.
* Undo and redo bookmark modifications.
* Save bookmark changes directly into PDF files.

These features are designed to improve document navigation and maintain bookmark portability between different PDF applications.

### Apidox

The original Okular API documentation is available at:

https://api.kde.org/okular/html/index.html

## Contributing

MobiDoc is an independent fork of Okular.

Contributions, improvements, and bug fixes are welcome.

The original Okular project uses a merge request workflow on KDE GitLab.

For information about contributing to KDE projects, see:

https://community.kde.org/Infrastructure/GitLab

### Build instructions

MobiDoc uses the KDE Frameworks and CMake build system inherited from Okular.

See https://community.kde.org/Get_Involved/development for an introduction to setting up a KDE development environment.

After obtaining the MobiDoc source code and configuring the required dependencies, the project can be built using CMake:

```bash
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir ..
make
make install
```

The project also builds tests in the build tree. Depending on the environment, running the tests may require installing the application first.

If installed in a custom directory, it may be necessary to run:

```bash
source prefix.sh
```

This ensures that the correct application instance and libraries are used.

MobiDoc retains the original Okular build configuration options where applicable.

For example:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir -DOKULAR_UI=desktop ..
```

Available options are `desktop`, `mobile`, and `both`.

### clang-format

MobiDoc inherits the clang-format configuration from Okular to maintain consistent source code formatting.

See [README.clang_format](https://invent.kde.org/graphics/okular/-/blob/master/README.clang-format) for more information.

## Credits

MobiDoc is based on **Okular**, the universal document viewer developed by the KDE community.

Original project:

https://okular.kde.org

Source code:

https://invent.kde.org/graphics/okular

MobiDoc builds upon the work of the original Okular developers and contributors.

The original authors and copyright holders retain their respective rights.

## License

MobiDoc is distributed under the applicable open-source licenses inherited from the Okular project.

All original copyright notices, license files, and attribution requirements are preserved.

For detailed licensing information, refer to the license files included in the source code.

MobiDoc is an independent project and is not an official KDE application.
