# JKQTMathText

LaTeX math formula rendering for Qt6, extracted from [JKQtPlotter](https://github.com/jkriege2/JKQtPlotter).

## Original Author

Copyright (c) 2008-2024 Jan W. Krieger (<jan@jkrieger.de>)

JKQTMathText was originally developed as part of JKQtPlotter by Jan W. Krieger
at the German Cancer Research Center (DKFZ) and the University of Heidelberg.

## License

LGPL-2.1-or-later (GNU Lesser General Public License)

See the original license headers in each source file.

## What This Is

This is a standalone extraction of the JKQTMathText component for use in
Corbomite. It renders LaTeX math formulas directly via QPainter -- no external
TeX installation required.

## Features

- Inline math: `$E = mc^2$`
- Display math: `$$\int_a^b f(x)\,dx$$`
- Supports ~93% of MathJax/KaTeX math commands
- Pure C++/Qt, no external dependencies
- Renders via QPainter (vector graphics, scalable)
- Includes XITS and Fira Math fonts
