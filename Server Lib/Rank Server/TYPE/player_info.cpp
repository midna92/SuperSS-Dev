// File player_info.cpp
// Created on 15/06/2020 at 15:09 by Acrisio
// Implementation of the class PlayerInfo

#if defined(_WIN32)
#pragma pack(1)
#endif

#include "player_info.hpp"

using namespace stdA;

PlayerInfo::PlayerInfo() : player_info(0u), m_state(0u), m_sd(0u) {
}

PlayerInfo::~PlayerInfo() {
	clear();
}

void PlayerInfo::clear() {

	player_info::clear();

	m_state = 0u;

	m_sd.clear();
}
