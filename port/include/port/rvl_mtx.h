// Wii matrix-library types that Aurora's headers don't define.
#pragma once

#include <dolphin/mtx.h>
#include <dolphin/mtx/mtx44ext.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef f32 Mtx23[2][3];
typedef f32 (*Mtx23P)[3];
typedef f32 Mtx33[3][3];
typedef f32 (*Mtx3P)[3];

#ifdef __cplusplus
}
#endif

/* SMG1 uses the paired single versions of matrix / vector operations */
#ifndef VECNormalize
#define VECNormalize PSVECNormalize
#endif
#ifndef VECMag
#define VECMag PSVECMag
#endif

// Aurora maps the paired-single 3x4 names to its C implementations on the host
// but not the 4x4 ones.
#ifndef GEKKO
#define PSMTX44Identity C_MTX44Identity
#define PSMTX44Copy C_MTX44Copy
#define PSMTX44Concat C_MTX44Concat
#define PSMTX44Transpose C_MTX44Transpose
#define PSMTX44Trans C_MTX44Trans
#define PSMTX44TransApply C_MTX44TransApply
#define PSMTX44Scale C_MTX44Scale
#define PSMTX44ScaleApply C_MTX44ScaleApply
#define PSMTX44RotRad C_MTX44RotRad
#define PSMTX44RotTrig C_MTX44RotTrig
#define PSMTX44RotAxisRad C_MTX44RotAxisRad
#endif
