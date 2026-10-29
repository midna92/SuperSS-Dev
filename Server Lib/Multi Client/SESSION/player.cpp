// File player.cpp
// Created on 22/10/2017 by Acrisio
// Implementation of class player

#include <WinSock2.h>
#include <Windows.h>
#include "player.hpp"
#include <memory.h>

using namespace stdA;

player::player(threadpool_base& _threadpool) : session(_threadpool), m_ci() {
	
};

player::~player() {
	
};

unsigned char player::getStateLogged() {
	return 0;	// not used here, only in the login and game server
};

unsigned long player::getUID() {
	return m_ci.m_uid;
};

unsigned long player::getCapability() {
	return m_ci.m_cap;
};

char* player::getNickname() {
	return m_ci.m_nickname;
};

char* player::getID() {
	return m_ci.m_id;
};
