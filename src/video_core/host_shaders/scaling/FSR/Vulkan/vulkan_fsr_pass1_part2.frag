#define FSR_RCAS_F 1
AU4 con0;

AF4 FsrRcasLoadF(ASU2 p) { return AF4(texelFetch(color_texture, p, 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

// #include "ffx_fsr1.h"