/*
 * rtffilteroptions.h — Filter options for selective RTF clipboard export
 *
 * Controls which style attributes are included when copying content
 * as RTF via the "Copy with Style Options..." dialog.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PENELOPE_RTFFILTEROPTIONS_H
#define PENELOPE_RTFFILTEROPTIONS_H

struct RtfFilterOptions {
    // Character-level
    bool includeFonts       = true;  // \f, \fs
    bool includeEmphasis    = true;  // \b, \i, \ul, \strike
    bool includeScripts     = true;  // \super, \sub
    bool includeTextColor   = true;  // \cf
    bool includeHighlights  = true;  // \highlight, \cbpat, \clcbpat

    // Paragraph-level
    bool includeAlignment   = true;  // \ql, \qc, \qr, \qj
    bool includeSpacing     = true;  // \sb, \sa, \sl, \slmult1
    bool includeMargins     = true;  // \li, \ri, \fi

    // Source formatting — per-word/span style differences
    bool includeSourceFormatting = true; // when OFF, all text in a block
                                        // uses uniform base style (first
                                        // TextRun's style), stripping
                                        // individual bold/italic/code/link
                                        // styling differences

    // Preset factories
    static RtfFilterOptions fullStyle()
    {
        return {}; // all true by default
    }

    static RtfFilterOptions noColors()
    {
        RtfFilterOptions opts;
        opts.includeTextColor = false;
        opts.includeHighlights = false;
        return opts;
    }

    static RtfFilterOptions fontsAndSizes()
    {
        RtfFilterOptions opts;
        opts.includeTextColor = false;
        opts.includeHighlights = false;
        opts.includeAlignment = false;
        opts.includeSpacing = false;
        opts.includeMargins = false;
        return opts;
    }

    static RtfFilterOptions structureOnly()
    {
        RtfFilterOptions opts;
        opts.includeFonts = false;
        opts.includeEmphasis = false;
        opts.includeScripts = false;
        opts.includeTextColor = false;
        opts.includeHighlights = false;
        opts.includeSpacing = false;
        opts.includeSourceFormatting = false;
        return opts;
    }
};

#endif // PENELOPE_RTFFILTEROPTIONS_H
