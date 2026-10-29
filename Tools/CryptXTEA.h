// File CryptXTEA.h
// Created on 02/10/2015 by Acrisio
// Definition and implementation of the class CryptXTEA

#pragma once
#ifndef CRYPTXTEA_H
#define CRYPTXTEA_H

#include <iostream>
#include <fstream>

namespace stdA {

	class CryptXTEA {
		public:
			CryptXTEA();
			~CryptXTEA();

			/*Src and Dst - IO*/
			int OpenFile(unsigned char*& buffer, char* path, unsigned int* tamanho, int option);

			void EncryptXTEA(unsigned int* keys, unsigned int* dst, unsigned int* src);

			void DecryptXTEA(unsigned int* keys, unsigned int* dst, unsigned int* src);

			void EncryptUpdatelist(unsigned char* dst, unsigned char* src, unsigned int tamanho, int option);

			void DecryptUpdatelist(unsigned char* dst, unsigned char* src, unsigned int tamanho, int option);
	};
}

#endif