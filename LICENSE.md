# Licensing

Mango is a JUCE plugin built on [FxmeTools](https://github.com/odoare/FxmeTools)
(vendored as the `lib/FxmeTools` submodule), which is itself split into a
framework-free half and a JUCE half under different licences. Which one
applies to a given file in this repository is stated in that file's own
header as an SPDX identifier; this document explains why, mirroring
`lib/FxmeTools/LICENSE.md`.

```
Almost everything in Source/ and Tests/   AGPL-3.0-or-later, or commercial terms
Source/Dsp/OverrideParser.h               LGPL-3.0-or-later
Source/Dsp/HeldNotes.h                    LGPL-3.0-or-later
Tests/UnitTests.cpp                       LGPL-3.0-or-later
```

## The plugin itself — AGPL-3.0-or-later, or commercial

Mango's GUI, DSP glue, processor and editor all compile against JUCE and
against `lib/FxmeTools/FxmeTools/` (FxmeTools' own JUCE module). JUCE 8 is
itself dual-licensed - AGPLv3, or a commercial JUCE licence - and FxmeTools'
JUCE module mirrors that shape rather than fighting it. Distributing Mango
therefore means one of:

    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial

- **Under the AGPLv3** (full text in `LICENSE`): use, modify and distribute
  freely, including as a hosted service, provided the complete corresponding
  source is offered to anyone who receives the plugin (or uses it over a
  network). This pairs with using JUCE under its own AGPLv3 option.
- **Under commercial terms**: available from the author for anyone holding a
  commercial JUCE licence who does not wish to release under the AGPL.
  `LicenseRef-FXME-Commercial` refers to the same commercial terms FxmeTools
  itself offers; contact via [github.com/odoare](https://github.com/odoare)
  or www.fx-mechanics.com. A commercial grant here does not include a JUCE
  licence - that is separate, from Raw Material Software.

## The three framework-free files — LGPL-3.0-or-later

`Source/Dsp/OverrideParser.h` (the block-override mini-language parser),
`Source/Dsp/HeldNotes.h` (MIDI voice tracking for it) and
`Tests/UnitTests.cpp` (the console unit tests covering both, run without a
JUCE checkout) do not include a JUCE header, link a JUCE library, or name a
JUCE symbol. They are not a derivative of JUCE, so their licence is
independent of it:

    SPDX-License-Identifier: LGPL-3.0-or-later

Full text in `LICENSE.LGPL`. LGPLv3 is written as additional permissions on
top of GPLv3, so `LICENSE.LGPL.GPL` carries the GPLv3 text it incorporates;
the two are read together. Anyone is free to lift these two headers into a
non-JUCE project under the LGPL alone - which is exactly why they were kept
JUCE-free in the first place.

---

Copyright (c) 2026 Olivier Doaré.
