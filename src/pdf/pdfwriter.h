/*
 * pdfwriter.h — Low-level PDF writer
 *
 * Extracted from Scribus (Andreas Vox, 2014) and simplified:
 *   - No encryption, no PDFVersion enum, no ScStreamFilter
 *   - Hardcoded PDF-1.7
 *   - In-memory QByteArray output alongside file output
 *   - Simple QHash resource dictionaries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_PDFWRITER_H
#define PENELOPE_PDFWRITER_H

#include <type_traits>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QList>
#include <QString>

namespace Pdf {

using ObjId = uint32_t;

// --- PDF serialization helpers (cf. PDF32000-2008) ---

bool isDelimiter(char c);

uchar toPdfDocEncoding(QChar c);
QByteArray toPdfDocEncoding(const QString &s);

QByteArray toUTF16(const QString &s);
template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true>
inline QByteArray toPdf(T v) { return QByteArray::number(static_cast<qlonglong>(v)); }

template <typename T, std::enable_if_t<std::is_floating_point_v<T>, bool> = true>
inline QByteArray toPdf(T v) { return QByteArray::number(v, 'f', 6); }

QByteArray toObjRef(ObjId id);

QByteArray toLiteralString(const QByteArray &s);
QByteArray toLiteralString(const QString &s);

QByteArray toHexString(const QByteArray &s);
QByteArray toHexString16(quint16 b);

QByteArray toName(const QByteArray &s);
QByteArray toName(const QString &s);

QByteArray toDateString(const QDateTime &dt);

// --- Resource dictionary (simplified from Scribus) ---

struct ResourceDict {
    QHash<QByteArray, ObjId> fonts;
    QHash<QByteArray, ObjId> xObjects;
    QHash<QByteArray, ObjId> extGState;
};

// --- PDF Writer ---

class Writer {
public:
    Writer();

    // Output targets (mutually exclusive)
    bool openFile(const QString &filename);
    bool openBuffer(QByteArray *buffer);
    bool close(bool aborted = false);

    // PDF structure
    void writeHeader();
    void writeXrefAndTrailer();
    void write(const QByteArray &bytes);
    void writeResourceDict(const ResourceDict &dict);

    // Object management
    ObjId reserveObjects(unsigned int n);
    ObjId newObject() { return reserveObjects(1); }
    void startObj(ObjId id);
    ObjId startObj();
    void endObj(ObjId id);
    void endObjectWithStream(ObjId id, const QByteArray &streamContent,
                             bool compress = true);

    // Well-known object IDs (assigned during writeHeader)
    ObjId catalogObj() const { return m_catalogObj; }
    ObjId infoObj() const { return m_infoObj; }
    ObjId pagesObj() const { return m_pagesObj; }

private:
    ObjId m_objCounter = 0;
    ObjId m_currentObj = 0;

    // Output: either file or buffer
    QFile m_file;
    QByteArray *m_buffer = nullptr;
    bool m_usingBuffer = false;

    QList<qint64> m_xref;
    qint64 m_bytesWritten = 0;

    // Well-known objects
    ObjId m_catalogObj = 0;
    ObjId m_infoObj = 0;
    ObjId m_pagesObj = 0;

    QByteArray m_fileId;

    void writeRaw(const QByteArray &bytes);
};

} // namespace Pdf

#endif // PENELOPE_PDFWRITER_H
