/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pdfbookmarks.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QFile>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

static_assert(QPDF_MAJOR_VERSION >= 11, "PDF bookmark editing requires QPDF 11 or newer");

#ifdef Q_OS_WIN
static std::unique_ptr<FILE, decltype(&std::fclose)> openQFileHandleForQpdf(QFile &file, bool write)
{
    HANDLE duplicate = nullptr;
    const intptr_t nativeHandle = _get_osfhandle(file.handle());
    const HANDLE original = reinterpret_cast<HANDLE>(nativeHandle);
    if (nativeHandle == -1 ||
        !DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        throw std::runtime_error("Could not duplicate a temporary PDF file handle for QPDF");
    }

    const int flags = (write ? _O_WRONLY : _O_RDONLY) | _O_BINARY;
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(duplicate), flags);
    if (descriptor == -1) {
        CloseHandle(duplicate);
        throw std::runtime_error("Could not create a QPDF stream for a temporary PDF file");
    }

    FILE *stream = _fdopen(descriptor, write ? "wb" : "rb");
    if (!stream) {
        _close(descriptor);
        throw std::runtime_error("Could not create a QPDF stream for a temporary PDF file");
    }
    return {stream, &std::fclose};
}
#endif

void writePdfBookmarkTree(const char *input, const char *output, const char *password, const char *original, const char *originalPassword, const std::vector<PdfBookmarkNode> &tree)
{
    using Object = QPDFObjectHandle;
    QPDF pdf, source;
#ifdef Q_OS_WIN
    QFile inputFile(QFile::decodeName(input));
    QFile originalFile(QFile::decodeName(original));
    if (!inputFile.open(QIODevice::ReadOnly) || !originalFile.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Could not open a PDF file for saving bookmarks");
    }
    auto inputStream = openQFileHandleForQpdf(inputFile, false);
    auto originalStream = openQFileHandleForQpdf(originalFile, false);
    pdf.processFile(input, inputStream.get(), false, password);
    source.processFile(original, originalStream.get(), false, originalPassword);
#else
    pdf.processFile(input, password);
    source.processFile(original, originalPassword);
#endif
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    auto oldPages = QPDFPageDocumentHelper(source).getAllPages();
    if (pages.size() != oldPages.size()) {
        throw std::runtime_error("The PDF page structure changed");
    }
    std::map<QPDFObjGen, Object> copied;
    for (size_t i = 0; i < pages.size(); ++i) {
        copied.emplace(oldPages[i].getObjectHandle().getObjGen(), pages[i].getObjectHandle());
    }
    std::map<std::string, Object> originals, objects;
    std::set<QPDFObjGen> visited;
    std::function<void(Object, std::string, int)> read = [&](Object item, std::string prefix, int depth) {
        if (depth > 256) {
            throw std::runtime_error("PDF outline is too deeply nested");
        }
        int row = 0;
        while (!item.isNull()) {
            if (!item.isDictionary() || !item.isIndirect() || !visited.insert(item.getObjGen()).second) {
                throw std::runtime_error("Invalid or cyclic PDF outline");
            }
            const std::string id = prefix + std::to_string(row++);
            originals.emplace(id, item);
            read(item.getKey("/First"), id + ".", depth + 1);
            item = item.getKey("/Next");
        }
    };
    auto oldRoot = source.getRoot().getKey("/Outlines");
    if (oldRoot.isDictionary()) {
        read(oldRoot.getKey("/First"), "pdf:", 0);
    }
    auto root = pdf.makeIndirectObject(Object::newDictionary());
    root.replaceKey("/Type", Object::newName("/Outlines"));
    if (oldRoot.isIndirect()) {
        copied.emplace(oldRoot.getObjGen(), root);
    }
    std::function<void(const std::vector<PdfBookmarkNode> &, int)> allocate = [&](const auto &nodes, int depth) {
        if (depth > 256) {
            throw std::runtime_error("PDF outline is too deeply nested");
        }
        for (const auto &node : nodes) {
            const auto old = originals.find(node.id);
            if (node.id.empty() || objects.count(node.id) || (old == originals.end() && (node.id.rfind("new:", 0) != 0 || node.title.empty() || node.page < 0 || size_t(node.page) >= pages.size()))) {
                throw std::runtime_error("Invalid PDF bookmark");
            }
            auto object = pdf.makeIndirectObject(Object::newDictionary());
            objects.emplace(node.id, object);
            if (old != originals.end()) {
                copied.emplace(old->second.getObjGen(), object);
            }
            allocate(node.children, depth + 1);
        }
    };
    allocate(tree, 0);
    std::function<Object(Object, int)> clone = [&](Object object, int depth) -> Object {
        if (depth > 256) {
            throw std::runtime_error("PDF bookmark metadata is too deeply nested");
        }
        if (object.isIndirect()) {
            auto found = copied.find(object.getObjGen());
            if (found != copied.end()) {
                return found->second;
            }
            if (object.isStream()) {
                auto result = pdf.newStream(object.getRawStreamData());
                copied.emplace(object.getObjGen(), result);
                result.replaceDict(clone(object.getDict(), depth + 1));
                return result;
            }
            auto result = pdf.makeIndirectObject(Object::newNull());
            copied.emplace(object.getObjGen(), result);
            auto direct = Object::newNull();
            // Keep original handles while walking containers, so nested indirect
            // page references are remapped to this document's pages.
            if (object.isDictionary()) {
                direct = Object::newDictionary();
                for (const auto &key : object.getKeys()) {
                    direct.replaceKey(key, clone(object.getKey(key), depth + 1));
                }
            } else if (object.isArray()) {
                direct = Object::newArray();
                for (int i = 0; i < object.getArrayNItems(); ++i) {
                    direct.appendItem(clone(object.getArrayItem(i), depth + 1));
                }
            }
            if (!object.isDictionary() && !object.isArray()) {
                direct = Object::parse(object.unparseResolved());
            }
            pdf.replaceObject(result.getObjGen(), direct);
            return result;
        }
        if (object.isDictionary()) {
            auto result = Object::newDictionary();
            for (const auto &key : object.getKeys()) {
                result.replaceKey(key, clone(object.getKey(key), depth + 1));
            }
            return result;
        }
        if (object.isArray()) {
            auto result = Object::newArray();
            for (int i = 0; i < object.getArrayNItems(); ++i) {
                result.appendItem(clone(object.getArrayItem(i), depth + 1));
            }
            return result;
        }
        return Object::parse(object.unparseResolved());
    };
    const std::set<std::string> structural = {"/Parent", "/Prev", "/Next", "/First", "/Last", "/Count"};
    if (oldRoot.isDictionary()) {
        for (const auto &key : oldRoot.getKeys()) {
            if (!structural.count(key) && key != "/Type") {
                root.replaceKey(key, clone(oldRoot.getKey(key), 0));
            }
        }
    }
    std::function<long long(const std::vector<PdfBookmarkNode> &, Object)> link = [&](const auto &nodes, Object parent) {
        long long visible = 0;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto &node = nodes[i];
            auto object = objects.at(node.id);
            auto old = originals.find(node.id);
            if (old != originals.end()) {
                for (const auto &key : old->second.getKeys()) {
                    if (!structural.count(key)) {
                        object.replaceKey(key, clone(old->second.getKey(key), 0));
                    }
                }
                object.replaceKey("/Title", Object::newUnicodeString(node.title));
            } else {
                object.replaceKey("/Title", Object::newUnicodeString(node.title));
                object.replaceKey("/Dest", Object::newArray({pages.at(node.page).getObjectHandle(), Object::newName("/Fit")}));
            }
            object.replaceKey("/Parent", parent);
            if (i) {
                object.replaceKey("/Prev", objects.at(nodes[i - 1].id));
            }
            if (i + 1 < nodes.size()) {
                object.replaceKey("/Next", objects.at(nodes[i + 1].id));
            }
            const auto descendants = link(node.children, object);
            if (!node.children.empty()) {
                object.replaceKey("/Count", Object::newInteger(node.open ? descendants : -descendants));
            }
            visible += 1 + (node.open ? descendants : 0);
        }
        if (!nodes.empty()) {
            parent.replaceKey("/First", objects.at(nodes.front().id));
            parent.replaceKey("/Last", objects.at(nodes.back().id));
        }
        return visible;
    };
    root.replaceKey("/Count", Object::newInteger(link(tree, root)));
    pdf.getRoot().replaceKey("/Outlines", root);
#ifdef Q_OS_WIN
    QFile outputFile(QFile::decodeName(output));
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error("Could not open a PDF output file for saving bookmarks");
    }
    auto outputStream = openQFileHandleForQpdf(outputFile, true);
    QPDFWriter writer(pdf, output, outputStream.get(), false);
#else
    QPDFWriter writer(pdf, output);
#endif
    writer.setPreserveEncryption(true);
    writer.write();
}
