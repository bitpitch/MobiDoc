/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../pdfbookmarks.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

using Object = QPDFObjectHandle;

static void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

static void createPdf(const std::string &path, bool withOutline, bool encrypted = false, bool cyclic = false)
{
    QPDF pdf;
    pdf.emptyPDF();
    QPDFPageDocumentHelper pages(pdf);
    for (int i = 0; i < 2; ++i) {
        auto page = pdf.makeIndirectObject(Object::parse("<< /Type /Page /MediaBox [0 0 300 400] /Resources <<>> >>"));
        page.replaceKey("/Contents", pdf.newStream("q 0.5 g 10 10 50 50 re f Q\n"));
        auto note = pdf.makeIndirectObject(Object::parse("<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (Keep this note) >>"));
        page.replaceKey("/Annots", Object::newArray({note}));
        pages.addPage(QPDFPageObjectHelper(page), false);
    }
    if (withOutline) {
        auto root = pdf.makeIndirectObject(Object::parse("<< /Type /Outlines /Count 3 >>"));
        auto first = pdf.makeIndirectObject(Object::parse("<< /Title (Chapter) /Count 1 /F 2 /C [1 0 0] >>"));
        auto child = pdf.makeIndirectObject(Object::parse("<< /Title (Nested link) /A << /S /URI /URI (https://example.org/) >> >>"));
        auto last = pdf.makeIndirectObject(Object::parse("<< /Title (Named destination) /Dest (chapter-two) >>"));
        const auto page = pages.getAllPages().at(1).getObjectHandle();
        auto names = Object::newDictionary();
        names.replaceKey("/Dests", Object::newDictionary());
        names.getKey("/Dests").replaceKey("/Names", Object::newArray({Object::newString("chapter-two"), Object::newArray({page, Object::newName("/Fit")})}));
        pdf.getRoot().replaceKey("/Names", names);
        first.replaceKey("/Parent", root);
        first.replaceKey("/First", child);
        first.replaceKey("/Last", child);
        first.replaceKey("/Dest", Object::newArray({page, Object::newName("/Fit")}));
        first.replaceKey("/Next", last);
        child.replaceKey("/Parent", first);
        last.replaceKey("/Parent", root);
        last.replaceKey("/Prev", first);
        if (cyclic) {
            last.replaceKey("/Next", first);
        }
        root.replaceKey("/First", first);
        root.replaceKey("/Last", last);
        pdf.getRoot().replaceKey("/Outlines", root);
    }
    QPDFWriter writer(pdf, path.c_str());
    if (encrypted) {
        writer.setR6EncryptionParameters("user", "owner", true, true, true, true, true, true, qpdf_r3p_full, true);
    }
    writer.write();
}

static std::vector<Object> rootItems(QPDF &pdf)
{
    std::vector<Object> result;
    auto root = pdf.getRoot().getKey("/Outlines");
    if (root.isNull()) {
        return result;
    }
    auto item = root.getKey("/First");
    while (!item.isNull()) {
        require(result.size() < 20, "Unexpected outline cycle");
        result.push_back(item);
        item = item.getKey("/Next");
    }
    return result;
}

static void checkExistingContent(QPDF &pdf, const std::string &title = "Chapter", const std::string &childTitle = "Nested link")
{
    auto items = rootItems(pdf);
    require(items[0].getKey("/Title").getUTF8Value() == title, "Existing title changed");
    require(items[0].getKey("/F").getIntValue() == 2, "Existing bookmark formatting changed");
    require(items[0].getKey("/C").getArrayNItems() == 3, "Existing bookmark color changed");
    auto child = items[0].getKey("/First");
    require(child.getKey("/Title").getUTF8Value() == childTitle, "Nested title changed");
    require(child.getKey("/Parent").isSameObjectAs(items[0]), "Nested parent changed");
    require(child.getKey("/A").getKey("/URI").getStringValue() == "https://example.org/", "Nested action lost");
    require(items[1].getKey("/Dest").getStringValue() == "chapter-two", "Named destination changed");
    require(pdf.getRoot().getKey("/Names").getKey("/Dests").getKey("/Names").getArrayNItems() == 2, "Destination name tree lost");
    for (auto page : QPDFPageDocumentHelper(pdf).getAllPages()) {
        auto object = page.getObjectHandle();
        require(object.getKey("/Annots").getArrayItem(0).getKey("/Contents").getStringValue() == "Keep this note", "Annotation lost");
        auto buffer = object.getKey("/Contents").getStreamData();
        require(std::string(reinterpret_cast<const char *>(buffer->getBuffer()), buffer->getSize()) == "q 0.5 g 10 10 50 50 re f Q\n", "Page content changed");
    }
}

static void runTests(const std::filesystem::path &directory)
{
    const auto input = (directory / "input.pdf").string();
    const auto output = (directory / "output.pdf").string();
    const auto savedAgain = (directory / "saved-again.pdf").string();
    const auto undone = (directory / "undone.pdf").string();
    const std::string title = "Segnalibro: caffè 日本語";
    const PdfBookmarkNode firstNew {"new:test-id-1", title, 1, true, {}};
    const PdfBookmarkNode secondNew {"new:test-id-2", "Another bookmark", 0, true, {}};
    // The save model contains the current titles, including unchanged originals.
    const std::vector<PdfBookmarkNode> originals = {{"pdf:0", "Chapter", -1, true, {{"pdf:0.0", "Nested link", -1, false, {}}}}, {"pdf:1", "Named destination", -1, false, {}}};
    auto tree = originals;
    tree.push_back(firstNew);
    tree.push_back(secondNew);

    createPdf(input, true);
    writePdfBookmarkTree(input.c_str(), output.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(output.c_str());
        auto items = rootItems(pdf);
        require(items.size() == 4, "Bookmark addition failed");
        require(items[2].getKey("/Title").getUTF8Value() == title, "Unicode title corrupted");
        require(items[2].getKey("/Dest").getArrayItem(0).isSameObjectAs(QPDFPageDocumentHelper(pdf).getAllPages()[1].getObjectHandle()), "Wrong bookmark page");
        require(items[2].getKey("/Prev").isSameObjectAs(items[1]), "Previous link missing");
        require(items[3].getKey("/Parent").isSameObjectAs(pdf.getRoot().getKey("/Outlines")), "Parent link missing");
        require(pdf.getRoot().getKey("/Outlines").getKey("/Count").getIntValue() == 5, "Visible count incorrect");
        checkExistingContent(pdf);
    }

    // Saving after reopening/swapping the backing file must be idempotent.
    writePdfBookmarkTree(output.c_str(), savedAgain.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(savedAgain.c_str());
        require(rootItems(pdf).size() == 4, "Repeated save duplicated bookmarks");
        checkExistingContent(pdf);
    }
    // Undo one addition, save, then redo and save again.
    tree.erase(tree.begin() + 2);
    writePdfBookmarkTree(savedAgain.c_str(), undone.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(undone.c_str());
        auto items = rootItems(pdf);
        require(items.size() == 3 && items.back().getKey("/Title").getUTF8Value() == secondNew.title, "Undo removed the wrong bookmark");
        require(items.back().getKey("/Prev").isSameObjectAs(items[1]), "Undo broke sibling links");
        checkExistingContent(pdf);
    }
    tree.insert(tree.begin() + 2, firstNew);
    writePdfBookmarkTree(undone.c_str(), output.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(output.c_str());
        auto items = rootItems(pdf);
        require(items.size() == 4, "Redo failed");
        require(items[2].getKey("/Title").getUTF8Value() == firstNew.title, "Redo changed bookmark order");
    }

    // Rename existing entries and retain destinations, formatting, nested actions,
    // page contents and annotations across repeated saves, undo and redo.
    const std::string renamedTitle = "Capitolo rinominato: caffè 日本語";
    const std::string renamedChildTitle = "Collegamento rinominato: 日本語";
    tree[0].title = renamedTitle;
    tree[0].children[0].title = renamedChildTitle;
    writePdfBookmarkTree(output.c_str(), savedAgain.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(savedAgain.c_str());
        checkExistingContent(pdf, renamedTitle, renamedChildTitle);
    }
    writePdfBookmarkTree(savedAgain.c_str(), output.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(output.c_str());
        require(rootItems(pdf).size() == 4, "Repeated rename save duplicated bookmarks");
        checkExistingContent(pdf, renamedTitle, renamedChildTitle);
    }
    tree[0].title = originals[0].title;
    tree[0].children[0].title = originals[0].children[0].title;
    writePdfBookmarkTree(output.c_str(), undone.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(undone.c_str());
        checkExistingContent(pdf);
    }
    tree[0].title = renamedTitle;
    tree[0].children[0].title = renamedChildTitle;
    writePdfBookmarkTree(undone.c_str(), output.c_str(), "", input.c_str(), "", tree);
    {
        QPDF pdf;
        pdf.processFile(output.c_str());
        checkExistingContent(pdf, renamedTitle, renamedChildTitle);
    }

    // A PDF without an outline acquires its first bookmark, which can also be undone.
    createPdf(input, false);
    writePdfBookmarkTree(input.c_str(), output.c_str(), "", input.c_str(), "", {firstNew});
    {
        QPDF pdf;
        pdf.processFile(output.c_str());
        require(rootItems(pdf).size() == 1, "First bookmark failed");
    }
    writePdfBookmarkTree(output.c_str(), undone.c_str(), "", input.c_str(), "", {});
    {
        QPDF pdf;
        pdf.processFile(undone.c_str());
        require(rootItems(pdf).empty(), "Undo of first bookmark failed");
    }

    createPdf(input, true, true);
    tree = originals;
    tree.push_back(secondNew);
    writePdfBookmarkTree(input.c_str(), output.c_str(), "user", input.c_str(), "user", tree);
    {
        QPDF pdf;
        pdf.processFile(output.c_str(), "user");
        require(pdf.isEncrypted(), "Encryption was removed");
        require(rootItems(pdf).size() == 3, "Encrypted bookmark addition failed");
        checkExistingContent(pdf);
    }

    // Bad input must fail before touching the destination file.
    auto requireRejected = [&](const std::vector<PdfBookmarkNode> &badEdits) {
        const auto protectedOutput = (directory / "protected-output.pdf").string();
        std::ofstream(protectedOutput) << "unchanged";
        bool rejected = false;
        try {
            writePdfBookmarkTree(input.c_str(), protectedOutput.c_str(), "", input.c_str(), "", badEdits);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "Invalid input was accepted");
        std::ifstream stream(protectedOutput);
        std::string contents;
        stream >> contents;
        require(contents == "unchanged", "Failure overwrote the output file");
    };
    createPdf(input, false);
    requireRejected({{"new:bad-page", "Invalid page", 99, true, {}}});
    createPdf(input, true, false, true);
    requireRejected({secondNew});
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() / ("okular-pdfbookmarks-" + std::to_string(std::random_device {}()));
    try {
        std::filesystem::create_directory(directory);
        runTests(directory);
        std::filesystem::remove_all(directory);
        std::cout << "PDF bookmark persistence tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
