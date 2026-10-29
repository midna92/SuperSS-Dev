// File player_info.cpp
// Created on 11/03/2018 at 14:12 by Acrisio
// Implementation of the PlayerInfo class

#if defined(_WIN32)
#pragma pack(1)
#endif

#include "player_info.hpp"

using namespace stdA;

PlayerInfo::PlayerInfo() {
    clear();
};

PlayerInfo::~PlayerInfo() {
    clear();
};

void PlayerInfo::clear() {

    player_info::clear();

    m_state = 0;
    m_place = 0;
	m_server_uid = 0;
};
