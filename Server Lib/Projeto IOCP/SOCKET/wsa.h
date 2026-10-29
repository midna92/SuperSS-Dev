// File wsa.h
// Created on 01/05/2017 by Acrisio
// Definition of class wsa

#pragma once
#ifndef _STDA_WSA_H_
#define _STDA_WSA_H_

#ifndef _WINSOCK2_H
	#define _WINSOCK2_H
	#include <winsock2.h>
#endif

namespace stdA {
    class wsa {
        public:
            wsa();
            ~wsa();

        private:
            WSADATA m_wsa; 
    };
}

#endif