// File blowfish.h
// Created on 25/03/2022 at 22:51 by Acrisio
// Definition of the blowfish encryption algorithm
// Copy with small modifications of Bruce Schneier's C source code
// fonte: https://www.schneier.com/academic/blowfish/

#pragma once
#ifndef _BLOWFISH_H
#define _BLOWFISH_H

#define MAXKEYBYTES 56          /* 448 bits */
// #define little_endian 1              /* Eg: Intel */
#define big_endian 1            /* Eg: Motorola */

#define N				16
#define KEYBYTES		8

#if defined(_WIN32)
// Default pack
#pragma pack(push, ggsrv)
#pragma pack( )
#endif

#include <cstdint>

struct _BLOWFISH_CTX {
	uint32_t P[N + 2];
	uint32_t S[4][256];
};

#if defined(_WIN32)
// Pop default pack
#pragma pack(pop, ggsrv)
#endif

uint32_t F(_BLOWFISH_CTX* _ctx, uint32_t x);

void Blowfish_Init(_BLOWFISH_CTX* _ctx, char _key[], int _keybytes);

void Blowfish_Encrypt(_BLOWFISH_CTX* _ctx, uint32_t *_xl, uint32_t *_xr);

void Blowfish_Decrypt(_BLOWFISH_CTX* _ctx, uint32_t *_xl, uint32_t *_xr);

#endif // !_BLOWFISH_H
