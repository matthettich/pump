# Third-party notices

…pump is © wake. It uses the following open-source components, under permissive licences (and Eigen, under the
Mozilla Public License 2.0, used unmodified):

## DPF — DISTRHO Plugin Framework (ISC)
Copyright (C) 2012-2025 Filipe Coelho <falktx@falktx.com>. https://github.com/DISTRHO/DPF
Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
granted, provided that the above copyright notice and this permission notice appear in all copies.
DPF's VST3 support uses its own "travesty" API definitions (ISC), not the Steinberg SDK. CLAP headers: MIT,
Copyright (c) 2014-2022 Alexandre Bique. DPF bundles NanoVG (zlib licence, Copyright (c) 2013 Mikko Mononen) and
pugl (ISC, Copyright 2012-2022 David Robillard).

## pffft (BSD-style FFTPACK licence)
Copyright (c) 2020 Dario Mambro, (c) 2019 Hayati Ayguen, (c) 2013 Julien Pommier, (c) 2004 the University
Corporation for Atmospheric Research ("UCAR"). Full text: common/third_party/pffft/LICENSE.txt.

## NAM core — NeuralAmpModelerCore (MIT), used by pump AMP
Copyright (c) 2023 Steven Atkinson. https://github.com/sdatkinson/NeuralAmpModelerCore
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files, to deal in the Software without restriction, subject to including the copyright notice and this
permission notice in all copies or substantial portions of the Software. Full text:
third_party/NeuralAmpModelerCore/LICENSE. It includes nlohmann/json (MIT, Copyright (c) 2013-2025 Niels Lohmann).

## Eigen (Mozilla Public License 2.0), used by the NAM core
https://eigen.tuxfamily.org. Source: the NAM core's Dependencies/eigen submodule (unmodified). Full text:
https://www.mozilla.org/MPL/2.0/.

## Fonts (SIL Open Font License 1.1)
Share Tech Mono — Copyright 2012 Carrois Type Design. JetBrains Mono — Copyright 2020 The JetBrains Mono Project
Authors. Full texts: common/fonts/OFL-*.txt.

## Methods
Filter design after M. Vicanek, "Matched Second Order Digital Filters" (2016); higher-order shelves after
M. Holters and U. Zölzer (2006); loudness to ITU-R BS.1770-4 and EBU R128 / Tech 3341-3342; noise-shaping
coefficients from S. Lipshitz et al. (1991). Pitch tracking after A. de Cheveigné
and H. Kawahara, "YIN, a fundamental frequency estimator for speech and music" (2002); linear prediction by the
Levinson-Durbin recursion; band-limited oscillators after V. Välimäki and A. Huovilainen (2007); pitch-synchronous overlap-add after E. Moulines and
F. Charpentier (1990).
