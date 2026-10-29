// File guild.hpp
// Created on 29/12/2019 at 11:47 by Acrisio
// Definition of class Guild

#pragma once
#ifndef _STDA_GUILD_HPP
#define _STDA_GUILD_HPP

#if defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>
#endif

#include "../SESSION/player.hpp"

namespace stdA {
	struct Guild {
	public:
		enum eTEAM : unsigned char {
			RED,
			BLUE,
		};

	public:
		explicit Guild(uint32_t _ul = 0u);
		Guild(uint32_t _uid, eTEAM _team);
		~Guild();

		void clear();

		eTEAM getTeam();
		uint32_t getUID();
		unsigned short getPoint();
		uint32_t getPangWin();
		uint64_t getPang();

		void setTeam(eTEAM _team);
		void setUID(uint32_t _uid);
		void setPoint(unsigned short _point);
		void setPangWin(uint32_t _pang_win);
		void setPang(uint64_t _pang);

		void addPlayer(player& _session);
		void deletePlayer(player* _session);

		player* findPlayerByUID(uint32_t _uid);
		player* findPlayerByOID(uint32_t _oid);
		player* getPlayerByIndex(uint32_t _index);

		uint32_t numPlayers();

	private:
		eTEAM m_team;						// Team the guild is in the room
		uint32_t m_uid;				// Guild UID
		unsigned short m_point;				// Guild points
		uint32_t m_pang_win;			// Pangs earned in the game
		uint64_t m_pang;			// Guild pangs

		std::vector< player* > v_players;	// Players

#if defined(_WIN32)
		CRITICAL_SECTION m_cs;				// Critical Section
#elif defined(__linux__)
		pthread_mutex_t m_cs;				// Critical Section
#endif
	};
}

#endif // !_STDA_GUILD_HPP
