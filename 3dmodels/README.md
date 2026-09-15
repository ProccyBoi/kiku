# Component 3D models

The public source tree intentionally does not redistribute the local component
3D-model bundle. Several models originate from manufacturer/library/EasyEDA
sources and the project does not have a single documented redistribution licence
covering the complete bundle.

The KiCad schematic and PCB remain the electrical source of truth and do not
require these optional models for fabrication or firmware builds. For local 3D
review, obtain the applicable models from the original manufacturer, KiCad
library or component-library source under its own licence and place them in this
directory using the filenames referenced by the board. Local model files are
ignored by Git. `Mechanical/REFERENCE_GEOMETRY.md` documents the exact local
reference workflow and the source audit command.

Project-generated enclosure, bare-PCB and manufacturing geometry remains under
`Mechanical/` and `Production/` where it is part of the kiku source tree. A
populated PCB STEP, frozen transformed component BREPs and fit-check assemblies
that embed those third-party models are intentionally local-only outputs.
