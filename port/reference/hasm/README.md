# Hand-written C versions of HASM routines (not built)

Readable C translations of some routines from soul-re's `HASM_2.s`
(`racos`, the three `G2Quat_*_S` routines, the collision routines and
`ApplyMatrix_S`). They are **not compiled**: the build uses the versions that
`tools/hasm2c.py` generates from the original assembly (see `cmake/Hasm.cmake`).

They are kept as a starting point for readable replacements. Before one goes
back into the build it should be checked against the recompiled original on
random inputs, since the first attempt at these produced wrong animation poses.
