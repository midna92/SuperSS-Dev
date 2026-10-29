// File player.cpp
// Created on 15/06/2020 at 14:56 by Acrisio
// Implementation of the class player

#if defined(_WIN32)
#pragma pack(1)
#endif

#if defined(_WIN32)
#include <WinSock2.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#endif

#include "player.hpp"

using namespace stdA;

player::player(threadpool_base& _threadpool) : session(_threadpool), m_pi() {
}

player::~player() {
	clear();
}

bool player::clear() {
	
	bool ret = false;

	if ((ret = session::clear())) // // Check whether it was cleared or is waiting for another thread to clear, which is using the session
		m_pi.clear();

	return ret;
}

unsigned char player::getStateLogged() {
	return m_pi.m_state;
}

uint32_t player::getUID() {
	return m_pi.uid;
}

uint32_t player::getCapability() {
	return m_pi.m_cap;
}

char* player::getNickname() {
	return m_pi.nickname;
}

char* player::getID() {
	return m_pi.id;
}
