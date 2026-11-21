// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 EmberBSD contributors.
// Original Network Lab illustrations. Host authoring tool using QtGui and its SVG image plugin.
// Run the tool with an output directory; the application loads the checked-in PNG files.
#include <QByteArray>
#include <QDir>
#include <QImage>
#include <cstdio>

static const char globe[] = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="102" height="97" viewBox="0 0 102 97">
  <g transform="rotate(-17 53 48)">
    <circle cx="53" cy="48" r="34" fill="#153c43" stroke="#579b9d" stroke-width="1.5"/>
    <path d="M23 33 Q53 46 83 33 M20 48 Q53 60 86 48 M26 66 Q53 74 80 66" fill="none" stroke="#418183"/>
    <ellipse cx="53" cy="48" rx="14" ry="34" fill="none" stroke="#438788"/>
    <ellipse cx="53" cy="48" rx="27" ry="34" fill="none" stroke="#438788"/>
    <path d="M53 14 V82" stroke="#75a4a4" fill="none"/>
    <path d="M26 29 L34 26 44 25 48 28 46 34 39 37 38 43 34 42 29 39 25 35z
             M39 44 L47 43 55 48 55 53 50 58 45 70 42 68 44 58 40 53z
             M61 33 L68 28 77 31 80 36 73 40 70 38 66 43 61 40z
             M60 42 L68 42 72 47 72 53 66 62 62 60 61 53 57 48z"
          fill="#72a898" opacity=".9"/>
  </g>
  <path d="M23 40 C-5 61 7 73 37 65 C58 60 82 45 91 31 C100 16 85 15 73 21"
        fill="none" stroke="#75e6cf" stroke-width="1.7" stroke-linecap="round"/>
  <circle cx="20" cy="65" r="5" fill="#1e4b51" stroke="#a7f4d9"/>
  <circle cx="20" cy="65" r="2" fill="#d8ffe8"/>
  <path d="M84 75h6m-3-3v6 M15 18h4m-2-2v4" fill="none" stroke="#608d8c"/>
</svg>)SVG";

static const char route[] = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="80" height="70" viewBox="0 0 80 70">
  <g fill="none" stroke="#795951" stroke-width="1.4" stroke-linecap="round">
    <path d="M9 38h15l10-19h17l11-9h9 M25 38l12 16h15l11 8h7 M35 19l8 19h19 M37 54l7-16"/>
  </g>
  <path d="M9 38h16l10-19h16l11-9h9" fill="none" stroke="#ffad70" stroke-width="2.3" stroke-linejoin="round"/>
  <g fill="#382b2b" stroke="#a98068">
    <circle cx="44" cy="38" r="3"/><circle cx="62" cy="38" r="3"/>
    <circle cx="37" cy="54" r="3"/><circle cx="70" cy="62" r="3"/>
  </g>
  <g fill="#ffba85" stroke="#ffcb8a">
    <circle cx="9" cy="38" r="4"/><circle cx="35" cy="19" r="3"/><circle cx="51" cy="19" r="2"/>
  </g>
  <circle cx="70" cy="10" r="7" fill="#4e3934" stroke="#ffb477"/>
  <circle cx="70" cy="10" r="3" fill="#ffe3ac"/>
</svg>)SVG";

static QByteArray ruler()
{
    QByteArray svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="299" height="20">)";
    for (int i = 0; i < 60; ++i) {
        const int height = (i % 15 == 0 || i == 59) ? 16 : (i % 5 == 0 ? 10 : 5);
        svg += "<path d='M" + QByteArray::number(5 * i + 1) + ".5 2v" +
            QByteArray::number(height) + "' stroke='#749392'/>";
    }
    return svg + "</svg>";
}

static bool save(const QDir &directory, const char *name, const QByteArray &svg)
{
    const QImage image = QImage::fromData(svg, "svg").convertToFormat(QImage::Format_RGBA8888);
    if (!image.isNull() && image.save(directory.filePath(QString::fromLatin1(name))))
        return true;
    std::fprintf(stderr, "Failed to render %s\n", name);
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s output-directory\n", argv[0]);
        return 2;
    }
    const QDir out(QString::fromLocal8Bit(argv[1]));
    return save(out, "utc-orbit.png", globe) && save(out, "dns-route.png", route) &&
        save(out, "ruler.png", ruler()) ? 0 : 1;
}
