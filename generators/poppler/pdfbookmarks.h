/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_PDFBOOKMARKS_H
#define OKULAR_PDFBOOKMARKS_H

#include <string>
#include <vector>

struct PdfBookmarkNode {
    std::string id;
    std::string title;
    int page = -1;
    bool open = true;
    std::vector<PdfBookmarkNode> children;
};

// Rebuilds the desired outline, retaining original entry metadata and page
// destinations from the session template. Throws before writing on invalid input.
void writePdfBookmarkTree(const char *input, const char *output, const char *password, const char *original, const char *originalPassword, const std::vector<PdfBookmarkNode> &tree);

#endif
