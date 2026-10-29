// File team.hpp
// Created on 02/11/2018 at 14:09 by Acrisio
// Definition of the Team class

#pragma once
#ifndef _STDA_TEAM_HPP
#define _STDA_TEAM_HPP

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>
#endif

#include "../TYPE/game_type.hpp"
#include "../SESSION/player.hpp"
#include <map>
#include <string>

namespace stdA {
	class Team {
		public:
			struct team_ctx {
				team_ctx(uint32_t _ul = 0u) {
					clear();
				};
				void clear() { memset(this, 0, sizeof(team_ctx)); };
				uint32_t point;				// This is the team score
				unsigned short degree;				// Team angle
				unsigned char player_start_hole;	// Player who started the Hole
				unsigned char acerto_hole;			// Flag that hit the hole
				unsigned char hole;					// Hole the team is on
				unsigned char win : 1;				// Flag that tells whether the player won the previous hole
				unsigned short finish;				// State finish Hole, 9 finish with int32_t putt, 10 finish with chip-in
				unsigned char quit : 1;				// Player or the Team gave up
				GameData data;
				Location location;
			};

		public:
			Team(const int32_t _id);
			virtual ~Team();

			void addPlayer(player* _player);
			void deletePlayer(player* _player, int _option);

			// finders
			player* findPlayerByOID(int32_t _oid);
			player* findPlayerByUID(uint32_t _uid);
			player* findPlayerByNickname(std::string& _nickname);

			// Gets and Sets
			std::vector< player* >& getPlayers();

			const uint32_t getNumPlayers();

			const int32_t getId();
			void setId(const int32_t _id);

			const uint32_t getPoint();
			void setPoint(const uint32_t _point);

			const unsigned short getDegree();
			void setDegree(const unsigned short _degree);

			const Location& getLocation();
			void setLocation(const Location& _location);

			const unsigned char getAcertoHole();
			void setAcertoHole(const unsigned char _acerto_hole);

			const unsigned char getHole();
			void setHole(const unsigned char _hole);

			const char getGiveUp();
			void setGiveUp(const unsigned char _giveup);

			const uint32_t getTimeout();
			void setTimeout(const uint32_t _timeout);

			const uint32_t getTacadaNum();
			void setTacadaNum(const uint32_t _tacada_num);

			const uint32_t getTotalTacadaNum();
			void setTotalTacadaNum(const uint32_t _total_tacada_num);

			const uint64_t getPang();
			void setPang(const uint64_t _pang);

			const uint64_t getBonusPang();
			void setBonusPang(const uint64_t _bonus_pang);

			const uint32_t getBadCondute();
			void setBadCondute(const uint32_t _bad_condute);

			const int32_t getScore();
			void setScore(const int32_t _score);

			const unsigned char getLastWin();
			void setLastWin(const unsigned char _win);

			const unsigned char getPlayerStartHole();
			void setPlayerStartHole(const unsigned char _player_start_hole);

			const unsigned short getStateFinish();
			void setStateFinish(const unsigned short _finish);

			// Team gave up
			const unsigned char isQuit();
			void setQuit(const unsigned char _quit);

			// increment
			void incrementTacadaNum(uint32_t _inc = 1u);
			void incrementTotalTacadaNum(uint32_t _inc = 1u);
			void incrementPlayerStartHole(unsigned char _inc = 1u);
			void incrementPoint(uint32_t _inc = 1u);
			void incrementBadCondute(uint32_t _inc = 1u);
			void incrementPang(uint64_t _inc = 1ull);
			void incrementBonusPang(uint64_t _inc = 1ull);

			// decrement
			void decrementPlayerStartHole(unsigned char _dec = 1u);

			// returns the number of players in the team
			const uint32_t getCount();

			// Requests
			player* requestCalculePlayerTurn(uint32_t _seq_hole);

			// Sort
			void sort_player(uint32_t _uid);

		protected:
			virtual void clear_players();

		protected:
			std::vector< player* > m_players;

			player* m_player_turn;

			int32_t m_id;	// Color, 0 Red, 1 Blue
			
			// Team data
			team_ctx m_team_ctx;

#if defined(_WIN32)
			CRITICAL_SECTION m_cs;
#elif defined(__linux__)
			pthread_mutex_t m_cs;
#endif
	};
}

#endif // !_STDA_TEAM_HPP
