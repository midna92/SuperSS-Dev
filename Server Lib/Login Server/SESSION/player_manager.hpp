// File player_manager.h
// Created on 11/03/2018 at 14:18 by Acrisio
// Definition of the player_manager class

#pragma once
#ifndef _STDA_PLAYER_MANAGER_H
#define _STDA_PLAYER_MANAGER_H

#include <vector>
#include "player.hpp"
#include "../../Projeto IOCP/SOCKET/session_manager.hpp"

namespace stdA {
    class player_manager : public session_manager {
        public:
            player_manager(threadpool& _threapool, uint32_t _max_session);
            virtual ~player_manager();

			//virtual void clear() override;

			virtual player *findPlayer(uint32_t _uid, bool _oid = false);
    };
}

#endif