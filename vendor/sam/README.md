# Software Automatic Mouth source

This directory is adapted from [`s-macke/SAM`](https://github.com/s-macke/SAM)
at commit `a7b36efac730957b59471a42a45fd779f94d77dd`.

Local changes replace heap allocation with caller-provided output, add a sample
writer callback for bounded streaming on RP2040, reset renderer state between
phrases, add output bounds checks, and place working arrays in the RP2040
scratch banks.

The upstream repository provides no explicit open-source license and describes
the original software as reverse-engineered abandonware. The code is included
on this experimental branch with provenance intact; redistributors should make
their own licensing assessment.
