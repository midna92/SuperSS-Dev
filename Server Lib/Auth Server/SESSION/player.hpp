// File player.hpp
// Created on 02/12/2018 at 13:06 by Acrisio
// Definition of class player

#pragma once
#ifndef _STDA_PLAYER_HPP
#define _STDA_PLAYER_HPP

#include "../../Projeto IOCP/SOCKET/session.h"
#include "../TYPE/player_info.hpp"
#include "../../Projeto IOCP/THREAD POOL/threadpool_base.hpp"

namespace stdA {
	class player : public session {
		public:
			player(threadpool_base& _threadpool);
			virtual ~player();

			virtual bool clear() override;

			virtual unsigned char getStateLogged() override;

			virtual uint32_t getUID() override;
			virtual uint32_t getCapability() override;	// Tipo
			virtual char* getNickname() override;
			virtual char* getID() override;

		public:
			PlayerInfo m_pi;
	};
}

#endif // !_STDA_PLAYER_HPP
