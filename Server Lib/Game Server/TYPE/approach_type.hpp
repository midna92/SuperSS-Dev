// File approach_type.hpp
// Created on 12/06/2020 at 12:01 by Acrisio
// Definition of the types used in Approach

#if defined(_WIN32)
#pragma pack(1)
#endif

#pragma once
#ifndef _STDA_APPROACH_TYPE_HPP
#define _STDA_APPROACH_TYPE_HPP

#include <string>
#include "../../Projeto IOCP/PACKET/packet.h"
#include "game_type.hpp"

#include <cstdint>

namespace stdA {

#if defined(__linux__)
#pragma pack(1)
#endif

	enum eMISSION_TYPE : unsigned char {
		MT_NO_TYPE,
		MT_CO_OP,
		MT_FREE_FOR_ALL
	};

	struct mission_approach {

		public:
			mission_approach(uint32_t _ul = 0u) : nick("") {
				clear();
			};
			mission_approach(unsigned char _numero, unsigned char _box_qntd, eMISSION_TYPE _tipo, uint32_t _condition1, uint32_t _condition2, std::string _nick)
				: numero(_numero), box_qntd(_box_qntd), tipo(_tipo), condition{ _condition1, _condition2 }, nick(_nick) {
			};
			virtual ~mission_approach() {
				clear();
			};
			void clear() {

				numero = (unsigned char)0u;
				box_qntd = 0u;
				tipo = eMISSION_TYPE::MT_NO_TYPE;

				condition[0] = 0u;
				condition[1] = 0u;

				if (!nick.empty()) {
					nick.clear();
					nick.shrink_to_fit();
				}

			};
			void toPacket(packet& _packet) {

				_packet.addUint8(numero);
				_packet.addUint8(box_qntd);
				_packet.addUint8(tipo);
				_packet.addBuffer(condition, sizeof(condition));
				_packet.addString(nick);
			}

		public:
			unsigned char numero;			// Mission number
			unsigned char box_qntd;			// Number of boxes the mission gives
			eMISSION_TYPE tipo;				// Mission type = Co-op, free-for-all; Players Reward (one player, all players, Everybody who clears)
			uint32_t condition[2];		// Mission conditions, 1 and 2
			std::string nick;				// Player nickname if the mission has a condition for the player
	};

	struct mission_approach_ex : public mission_approach {
		public:
			mission_approach_ex(uint32_t _ul = 0u) : mission_approach(_ul), is_player_uid(false) {
			};
			virtual ~mission_approach_ex() {
				clear();
			};
			void clear() {

				mission_approach::clear();

				is_player_uid = false;
			};
			
		public:
			bool is_player_uid;		// Must set the player uid and the player nick
	};

	struct mission_approach_dados {
		public:
			union uMissionFlag{
				uint32_t flag;
				struct {
					uint32_t players : 5;		// The minimum number of players required to activate the mission, 0 allows all
					uint32_t condition1 : 13;	// Condition that will exist Ex: player rank 5 chip in completes the mission
					uint32_t condition2 : 13;	// Condition 2 is the same as 1 if the second is needed
				}bits;
			};

		public:
			mission_approach_dados(uint32_t _ul = 0u) {
				clear();
			};
			~mission_approach_dados() {
				clear();
			};
			void clear() {
				memset(this, 0, sizeof(mission_approach_dados));
			};

		public:
			uint32_t numero;
			uint32_t box;
			eMISSION_TYPE tipo;
			uint32_t reward_tipo;
			uMissionFlag flag;
	};

	struct approach_dados {
		public:
			enum eSTATUS : unsigned char {
				IN_GAME,			// Is in the game
				LEFT_GAME			// Left the game
			};

		public:
			approach_dados(uint32_t _ul = 0u) {
				clear();
			};
			virtual ~approach_dados() {
				clear();
			};
			void clear() {
				memset(this, 0, sizeof(approach_dados));
			};
			void setLeftGame() {

				status = eSTATUS::LEFT_GAME;
				position = (unsigned char)~0u;
				distance = (uint32_t)~0u;
				box = 0u;
				rank_box = 0u;
				time = 0u;
			};

		public:
			eSTATUS status;
			uint32_t oid;
			uint32_t uid;
			unsigned char position;		// Player position, -1 none
			uint32_t box;			// Number of boxes the player won
			uint32_t distance;		// Distance the player was from the hole, -1 if chip-in, timeout, OB or Water hazard
			uint32_t time;			// Player shot time, -1 if chip-in, timeout, OB or Water hazard
			unsigned short rank_box;	// Boxes by Top Rank and at the end of Approach
	};

	struct approach_dados_ex : public approach_dados {
		public:
			union uState {
				uState(unsigned char _uc = 0u) {
					ucState = _uc;
				};
				unsigned char ucState;
				struct {
					unsigned char chip_in : 1;
					unsigned char giveup : 1;
					unsigned char ob_or_water_hazard : 1;
					unsigned char timeout : 1;
				}stState;
			};

			enum eSTATE_QUIT : unsigned char {
				SQ_IN_GAME,
				SQ_QUIT_START,		// Quit but must be shown on the hole score board
				SQ_QUIT_ENDED		// Quit and it was already shown on the hole score board that he quit
			};

		public:
			approach_dados_ex(uint32_t _ul = 0u) : approach_dados(_ul),
				total_distance(0u), total_time(0u), total_box(0u), state(), state_quit(eSTATE_QUIT::SQ_IN_GAME) {
			};
			virtual ~approach_dados_ex() {
				clear();
			};
			void clear() {
				
				approach_dados::clear();

				total_distance = 0u;
				total_time = 0u;
				total_box = 0u;
				state.ucState = 0u;
				state_quit = eSTATE_QUIT::SQ_IN_GAME;
			};
			void toPacket(packet& _packet) {

				// status is the first addr of the function data
				//_packet.addBuffer(&status, sizeof(approach_dados) - sizeof(this)/*Sizeof pointer (Pointer of virtual table, vtable)*/);

				_packet.addUint8(status);
				_packet.addUint32(oid);
				_packet.addUint32(uid);
				_packet.addUint8(position);
				_packet.addUint32(box);

				// state.stState.chip_in || state.stState.giveup || state.stState.timeout || state.stState.ob_or_water_hazard
				if (state.ucState != 0u) {
					
					_packet.addUint32((uint32_t)~0u);
					_packet.addUint32(0u);
				
				}else {
					_packet.addUint32(distance);
					_packet.addUint32(time);
				}
				
				_packet.addUint16(rank_box);
			};
			void setLeftGame() {

				approach_dados::setLeftGame();

				state_quit = eSTATE_QUIT::SQ_QUIT_START;

			};

		public:
			uint32_t total_distance;
			uint32_t total_time;
			uint32_t total_box;
			uState state;
			eSTATE_QUIT state_quit;
	};

	// Polymorphism of struct PlayerGameInfo
	struct PlayerApproachInfo : public PlayerGameInfo {
		public:
			PlayerApproachInfo(uint32_t _ul = 0u) : PlayerGameInfo(_ul), m_app_dados{0} {
			};
			virtual ~PlayerApproachInfo() {
				m_app_dados.clear();
			};
			void clear() {
				
				// Clear base
				PlayerGameInfo::clear();

				// clear app dados
				m_app_dados.clear();
			};

		public:
			approach_dados_ex m_app_dados;		// Approach player data
	};

#if defined(__linux__)
#pragma pack()
#endif
}

#endif // !_STDA_APPROACH_TYPE_HPP
