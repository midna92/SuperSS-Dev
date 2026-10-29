// Arquivo dupla.hpp
// Created on 29/12/2019 at 11:43 by Acrisio
// Definition of the Dupla class

#pragma once
#ifndef _STDA_DUPLA_HPP
#define _STDA_DUPLA_HPP

#include "../TYPE/guild_type.hpp"
#include "../SESSION/player.hpp"

namespace stdA {
	struct Dupla {
	public:
		enum eSTATE : unsigned char {
			IN_GAME,
			OUT_GAME,
			OVER_TIME,	// Time ran out before the player finished the game
		};

	public:
		Dupla(unsigned char _numero, player *_p1, player *_p2);

		unsigned short sumScoreP1();		// Add the score of player 1
		unsigned short sumScoreP2();		// Add the score of player 2

		unsigned char numero;				// Pair number
		player *p[2];						// Players: [0] e [1]
		unsigned char hole[2];				// Number of the hole the player is on in the game: [0] and [1]
		uint32_t pang_win[2];			// Pang: [0] e [1]
		uint64_t pang[2];			// Total pangs earned in the game: [0] and [1]
		eSTATE state[2];					// State in the game: [0] and [1]
		Dados dados[2][18];					// Dados dos holes, 18 holes: [0] e [1]
	};
}

#endif // !_STDA_DUPLA_HPP
