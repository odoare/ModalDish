# Licensing

ModalDish is a JUCE plugin built on [FxmeTools](https://github.com/odoare/FxmeTools)
(vendored as the `lib/FxmeTools` submodule), which is itself split into a
framework-free half and a JUCE half under different licences. Which one
applies to a given file in this repository is stated in that file's own
header as an SPDX identifier; this document explains why, mirroring
`lib/FxmeTools/LICENSE.md`.

```
Almost everything in Source/ and Tests/   AGPL-3.0-or-later, or commercial terms
Source/ParamIDs.h                         LGPL-3.0-or-later
Source/Dsp/ModalModel.h                   LGPL-3.0-or-later
Tests/FemTests.cpp                        LGPL-3.0-or-later
Tests/juce_stub/JuceHeader.h              LGPL-3.0-or-later
```

## The plugin itself — AGPL-3.0-or-later, or commercial

ModalDish's GUI, DSP glue, processor and editor all compile against JUCE and
against `lib/FxmeTools/FxmeTools/` (FxmeTools' own JUCE module). JUCE 8 is
itself dual-licensed - AGPLv3, or a commercial JUCE licence - and FxmeTools'
JUCE module mirrors that shape rather than fighting it. Distributing ModalDish
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

## The four framework-free files — LGPL-3.0-or-later

These four do not include a JUCE header, link a JUCE library, or contain any
JUCE code. They are not a derivative of JUCE, so their licence is independent
of it:

    SPDX-License-Identifier: LGPL-3.0-or-later

- `Source/ParamIDs.h` — the parameter identifiers, the controller encoding and
  the velocity curve. Plain constants and one arithmetic helper.
- `Source/Dsp/ModalModel.h` — the immutable mesh-plus-modes value type shared
  with the audio thread. It names only the FxmeTools core types and the
  standard library.
- `Tests/FemTests.cpp` — the finite-element validation suite, which checks the
  mesher and the Morley plate eigensolver against closed-form answers and runs
  without a JUCE checkout.
- `Tests/juce_stub/JuceHeader.h` — a 44-line stand-in supplying the four
  numeric helpers (`jmin`, `jmax`, `jlimit`, `MathConstants`) that the modal
  synthesis uses, so it can be rendered offline with no JUCE at all. It
  declares them in a namespace named `juce` deliberately, since its whole
  purpose is to stand where the generated header would, but it includes no
  JUCE and copies none: the four are one-line templates for max, min, clamp
  and pi. Its existence is the evidence for the claim above about the DSP, and
  if it ever stops being enough to compile `PlateSynth.cpp` then the DSP has
  grown a real JUCE dependency and this document needs revisiting.

Full text in `LICENSE.LGPL`. LGPLv3 is written as additional permissions on
top of GPLv3, so `LICENSE.LGPL.GPL` carries the GPLv3 text it incorporates;
the two are read together. Anyone is free to lift these files into a non-JUCE
project under the LGPL alone - which is exactly why they were kept JUCE-free
in the first place.

Note that `Tests/IoTests.cpp` and `Tests/CascadeMeasure.cpp` also build and run
without JUCE, against that same stub, but they are **not** in this list: they
drive `Source/Dsp/PlateSynth.cpp`, which is written against JUCE and is
AGPL/commercial, so they derive from it whatever header satisfies them at build
time.

## Documentation

`doc/technical.tex` (and the PDF built from it) carries
`LGPL-3.0-or-later`, and that is deliberate rather than left over: it is a
write-up of the model, the discretisation and the numerical method, it derives
from no JUCE code, and the intent is that its content can be quoted and reused
alongside the framework-free half of the library it describes. The same goes
for the Markdown under `doc/`.

## The FEM library

The finite-element mesher and the plate eigensolver are not in this repository.
They live in FxmeTools' framework-free half
(`lib/FxmeTools/core/FxmeTools/acoustics/`) under `LGPL-3.0-or-later`, and are
covered by that submodule's own `LICENSE.md`.

---

Copyright (c) 2026 Olivier Doaré.
