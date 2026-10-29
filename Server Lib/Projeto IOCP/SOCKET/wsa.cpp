// File wsa.cpp
// Created on 01/05/2017 by Acrisio
// Implementation of class wsa

#include "wsa.h"
#include "../util/exception.h"
#include "../TYPE/stda_error.h"

using namespace stdA;

wsa::wsa() {
    WORD requestedVersion = MAKEWORD(2, 2);

    if (0 != WSAStartup(requestedVersion, &m_wsa))
        throw exception("Erro wsa::wsa()->WSAStartup()", STDA_MAKE_ERROR(STDA_ERROR_TYPE::WSA, 1, WSAGetLastError()));
}

wsa::~wsa() {
    WSACleanup();
}