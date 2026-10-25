"""Adapt only this checkout's private Craft recipe; fail on upstream drift."""
import ast
import sys
from pathlib import Path


def configure(text):
    marker = '# MOBIDOC_WINDOWS_PDF_ONLY'
    if marker in text:
        return text

    def replace(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise SystemExit(f'Craft recipe changed; expected one occurrence of: {old}')
        text = text.replace(old, new)

    replace('self.displayName = "Okular"', 'self.displayName = "MobiDoc"')
    replace('import info\n', 'import info\nimport os\nfrom pathlib import Path\n')
    replace('        super().__init__(**kwargs)', '''        super().__init__(**kwargs)
        # MOBIDOC_WINDOWS_PDF_ONLY
        self.buildRoot = lambda: Path(os.environ["MOBIDOC_CRAFT_BUILD_ROOT"])
        self.subinfo.options.configure.args += [
            "-DMOBIDOC_PDF_ONLY=ON", "-DOKULAR_UI=desktop", "-DUSE_DBUS=OFF",
            "-DFORCE_NOT_REQUIRED_DEPENDENCIES=KF6DocTools",
            "-DCMAKE_DISABLE_FIND_PACKAGE_KF6DocTools=ON",
        ]''')
    # Remove dependencies needed only for other document formats and media.
    # Keep Qt/KDE runtime dependencies of the retained PDF editor.
    excluded = {
        'libs/tiff', 'qt-libs/phonon', 'qt-libs/phonon-vlc',
        'kde/kdegraphics/kdegraphics-mobipocket', 'libs/chm',
        'libs/libspectre', 'libs/discount', 'libs/djvu', 'libs/ebook-tools',
        'libs/ghostscript', 'kde/applications/libkexiv2',
        'kde/frameworks/tier3/purpose',
    }
    lines = []
    for line in text.splitlines():
        if any(f'Dependencies["{name}"]' in line for name in excluded):
            # Keep conditional blocks syntactically valid after removal.
            lines.append(line[:len(line) - len(line.lstrip())] + 'pass')
        else:
            lines.append(line)
    text = '\n'.join(lines) + '\n'
    replace('self.addExecutableFilter(r"bin/(?!(okular|',
            'self.addExecutableFilter(r"bin/(?!(MobiDoc|')
    replace('self.defines["executable"] = r"bin\\okular.exe"',
            'self.defines["executable"] = r"bin\\MobiDoc.exe"')
    replace('self.defines["file_types"] = [".pdf", ".mobi", ".epub", ".tiff", ".djvu"]',
            'self.defines["file_types"] = [".pdf"]')
    replace('self.defines["alias"] = "okular"', 'self.defines["alias"] = "MobiDoc"')
    replace('icons/150-apps-okular.png', 'icons/128-apps-mobidoc.png')
    replace('icons/44-apps-okular.png', 'icons/48-apps-mobidoc.png')
    ast.parse(text)
    return text


if __name__ == '__main__':
    recipe = Path(sys.argv[1])
    result = configure(recipe.read_text(encoding='utf-8'))
    recipe.write_text(result, encoding='utf-8')

