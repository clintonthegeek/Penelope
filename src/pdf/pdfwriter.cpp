/*
 * pdfwriter.cpp — Low-level PDF writer
 *
 * Extracted from Scribus (Andreas Vox, 2014) and simplified.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pdfwriter.h"

#include <QCryptographicHash>
#include <cassert>
#include <zlib.h>

namespace Pdf {

// --- Character classification ---

bool isDelimiter(char c)
{
    return QByteArray("()<>[]{}/%").contains(c);
}

// --- PDF Doc Encoding (PDF32000-2008, 8.9.2 + Annex D) ---

uchar toPdfDocEncoding(QChar c)
{
    uchar row = c.row();
    uchar cell = c.cell();
    switch (row) {
    case 0:
        if (cell <= 23 || (cell >= 32 && cell <= 127) || cell >= 161)
            return cell;
        return 0;
    case 0x01:
        switch (cell) {
        case 0x92: return 134; case 0x41: return 149; case 0x52: return 150;
        case 0x60: return 151; case 0x78: return 152; case 0x7d: return 153;
        case 0x31: return 154; case 0x42: return 155; case 0x53: return 156;
        case 0x61: return 157; case 0x7e: return 158;
        default: return 0;
        }
    case 0x02:
        switch (cell) {
        case 0xc7: return 25; case 0xc6: return 26; case 0xd8: return 24;
        case 0xd9: return 27; case 0xda: return 30; case 0xdb: return 29;
        case 0xdc: return 31; case 0xdd: return 28;
        default: return 0;
        }
    case 0x20:
        switch (cell) {
        case 0x13: return 133; case 0x14: return 132; case 0x18: return 143;
        case 0x19: return 144; case 0x1a: return 145; case 0x1c: return 141;
        case 0x1d: return 142; case 0x1e: return 140; case 0x20: return 129;
        case 0x21: return 130; case 0x22: return 128; case 0x26: return 131;
        case 0x30: return 139; case 0x39: return 136; case 0x3a: return 137;
        case 0x44: return 135; case 0xac: return 160;
        default: return 0;
        }
    case 0x21:
        if (cell == 0x22) return 146;
        return 0;
    case 0x22:
        if (cell == 0x12) return 138;
        return 0;
    case 0xfb:
        switch (cell) {
        case 0x01: return 147; case 0x02: return 148;
        default: return 0;
        }
    }
    return 0;
}

QByteArray toPdfDocEncoding(const QString &s)
{
    QByteArray result;
    result.reserve(s.length());
    for (int i = 0; i < s.length(); ++i) {
        uchar pdfChar = toPdfDocEncoding(s[i]);
        if (pdfChar != 0 || s[i].isNull())
            result += static_cast<char>(pdfChar);
        else
            result += '?';
    }
    return result;
}

QByteArray toUTF16(const QString &s)
{
    QByteArray result;
    result.reserve(2 + s.length() * 2);
    result.append('\xfe');
    result.append('\xff');
    for (int i = 0; i < s.length(); ++i) {
        result.append(static_cast<char>(s[i].row()));
        result.append(static_cast<char>(s[i].cell()));
    }
    return result;
}

QByteArray toObjRef(ObjId id)
{
    return toPdf(id) + " 0 R";
}

QByteArray toLiteralString(const QByteArray &s)
{
    constexpr int lineLength = 80;
    QByteArray result("(");
    for (int i = 0; i < s.length(); ++i) {
        uchar v = s[i];
        if (v == '(' || v == ')' || v == '\\') {
            result.append('\\');
            result.append(static_cast<char>(v));
        } else if (v < 32 || v >= 127) {
            result.append('\\');
            result.append("01234567"[(v / 64) % 8]);
            result.append("01234567"[(v / 8) % 8]);
            result.append("01234567"[v % 8]);
        } else {
            result.append(static_cast<char>(v));
        }
        if (i % lineLength == lineLength - 1)
            result.append("\\\n");
    }
    result.append(')');
    return result;
}

QByteArray toLiteralString(const QString &s)
{
    return toLiteralString(toPdfDocEncoding(s));
}

QByteArray toHexString(const QByteArray &s)
{
    constexpr int lineLength = 80;
    QByteArray result("<");
    for (int i = 0; i < s.length(); ++i) {
        uchar v = s[i];
        result.append("0123456789ABCDEF"[v / 16]);
        result.append("0123456789ABCDEF"[v % 16]);
        if (i % lineLength == lineLength - 1)
            result.append('\n');
    }
    result.append('>');
    return result;
}

QByteArray toHexString16(quint16 b)
{
    QByteArray result("<");
    result.append("0123456789ABCDEF"[(b >> 12) & 0xf]);
    result.append("0123456789ABCDEF"[(b >> 8) & 0xf]);
    result.append("0123456789ABCDEF"[(b >> 4) & 0xf]);
    result.append("0123456789ABCDEF"[b & 0xf]);
    result.append('>');
    return result;
}

QByteArray toName(const QByteArray &s)
{
    QByteArray result("/");
    for (int i = 0; i < s.length(); ++i) {
        uchar c = s[i];
        if (c <= 32 || c >= 127 || c == '#' || isDelimiter(static_cast<char>(c))) {
            result.append('#');
            result.append("0123456789ABCDEF"[c / 16]);
            result.append("0123456789ABCDEF"[c % 16]);
        } else {
            result.append(static_cast<char>(c));
        }
    }
    return result;
}

QByteArray toName(const QString &s)
{
    return toName(toPdfDocEncoding(s));
}

QByteArray toDateString(const QDateTime &dt)
{
    return "D:" + dt.toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1() + "Z";
}

// --- Writer implementation ---

Writer::Writer()
{
    m_fileId = QCryptographicHash::hash(
        QDateTime::currentDateTime().toString().toUtf8(),
        QCryptographicHash::Md5);
}

bool Writer::openFile(const QString &filename)
{
    m_file.setFileName(filename);
    if (!m_file.open(QIODevice::WriteOnly))
        return false;
    m_usingBuffer = false;
    m_buffer = nullptr;
    m_bytesWritten = 0;
    m_objCounter = 4; // reserve 1=catalog, 2=info, 3=pages
    m_catalogObj = 1;
    m_infoObj = 2;
    m_pagesObj = 3;
    m_xref.clear();
    return true;
}

bool Writer::openBuffer(QByteArray *buffer)
{
    if (!buffer)
        return false;
    m_buffer = buffer;
    m_buffer->clear();
    m_usingBuffer = true;
    m_bytesWritten = 0;
    m_objCounter = 4;
    m_catalogObj = 1;
    m_infoObj = 2;
    m_pagesObj = 3;
    m_xref.clear();
    return true;
}

bool Writer::close(bool aborted)
{
    if (m_usingBuffer) {
        if (aborted)
            m_buffer->clear();
        m_buffer = nullptr;
        return !aborted;
    }
    bool ok = (m_file.error() == QFile::NoError);
    m_file.close();
    if (aborted || !ok) {
        if (m_file.exists())
            m_file.remove();
    }
    return ok && !aborted;
}

void Writer::writeRaw(const QByteArray &bytes)
{
    if (m_usingBuffer) {
        m_buffer->append(bytes);
    } else {
        m_file.write(bytes);
    }
    m_bytesWritten += bytes.size();
}

void Writer::write(const QByteArray &bytes)
{
    writeRaw(bytes);
}

void Writer::writeHeader()
{
    write("%PDF-1.7\n");
    write("%\xc7\xec\x8f\xa2\n"); // high-bit bytes to signal binary
}

void Writer::writeXrefAndTrailer()
{
    qint64 startXref = m_bytesWritten;
    write("xref\n");
    write("0 " + toPdf(m_objCounter) + "\n");
    for (int i = 0; i < m_xref.count(); ++i) {
        if (m_xref[i] > 0) {
            QByteArray offset = QByteArray::number(m_xref[i]);
            while (offset.length() < 10)
                offset.prepend('0');
            write(offset + " 00000 n \n");
        } else {
            write("0000000000 65535 f \n");
        }
    }
    write("trailer\n<<\n");
    write("/Size " + toPdf(m_xref.count()) + "\n");
    QByteArray idHex = toHexString(m_fileId);
    write("/Root " + toObjRef(m_catalogObj) + "\n");
    write("/Info " + toObjRef(m_infoObj) + "\n");
    write("/ID [" + idHex + idHex + "]\n");
    write(">>\nstartxref\n");
    write(toPdf(startXref) + "\n%%EOF\n");
}

void Writer::writeResourceDict(const ResourceDict &dict)
{
    write("<< /ProcSet [/PDF /Text /ImageB /ImageC /ImageI]\n");
    if (!dict.fonts.isEmpty()) {
        write("/Font <<\n");
        for (auto it = dict.fonts.begin(); it != dict.fonts.end(); ++it)
            write(toName(it.key()) + " " + toObjRef(it.value()) + "\n");
        write(">>\n");
    }
    if (!dict.xObjects.isEmpty()) {
        write("/XObject <<\n");
        for (auto it = dict.xObjects.begin(); it != dict.xObjects.end(); ++it)
            write(toName(it.key()) + " " + toObjRef(it.value()) + "\n");
        write(">>\n");
    }
    if (!dict.extGState.isEmpty()) {
        write("/ExtGState <<\n");
        for (auto it = dict.extGState.begin(); it != dict.extGState.end(); ++it)
            write(toName(it.key()) + " " + toObjRef(it.value()) + "\n");
        write(">>\n");
    }
    write(">>\n");
}

ObjId Writer::reserveObjects(unsigned int n)
{
    assert(n < (1u << 30));
    ObjId result = m_objCounter;
    m_objCounter += n;
    return result;
}

void Writer::startObj(ObjId id)
{
    assert(m_currentObj == 0);
    m_currentObj = id;
    while (static_cast<uint>(m_xref.length()) <= id)
        m_xref.append(0);
    m_xref[id] = m_bytesWritten;
    write(toPdf(id) + " 0 obj\n");
}

ObjId Writer::startObj()
{
    ObjId id = newObject();
    startObj(id);
    return id;
}

void Writer::endObj(ObjId id)
{
    assert(m_currentObj == id);
    m_currentObj = 0;
    write("\nendobj\n");
}

void Writer::endObjectWithStream(ObjId id, const QByteArray &streamContent, bool compress)
{
    assert(m_currentObj == id);

    QByteArray data;
    bool compressed = false;
    if (compress && streamContent.size() > 128) {
        // zlib compress
        uLongf destLen = compressBound(streamContent.size());
        data.resize(static_cast<int>(destLen));
        const int level = m_compressionLevel >= 0 ? m_compressionLevel : Z_DEFAULT_COMPRESSION;
        int zret = ::compress2(reinterpret_cast<Bytef *>(data.data()), &destLen,
                               reinterpret_cast<const Bytef *>(streamContent.data()),
                               streamContent.size(), level);
        if (zret == Z_OK) {
            data.resize(static_cast<int>(destLen));
            compressed = true;
        } else {
            data = streamContent;
        }
    } else {
        data = streamContent;
    }

    write("/Length " + toPdf(data.size()) + "\n");
    if (compressed) {
        write("/Filter /FlateDecode\n");
        write("/Length1 " + toPdf(streamContent.size()) + "\n");
    }
    write(">>\nstream\n");
    write(data);
    write("\nendstream");
    endObj(id);
}

} // namespace Pdf
