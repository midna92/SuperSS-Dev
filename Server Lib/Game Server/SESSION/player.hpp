// File player.hpp
// Created on 07/03/2018 at 21:58 by Acrisio
// Definition of class player polymorphism

#pragma once
#ifndef _STDA_PLAYER_HPP
#define _STDA_PLAYER_HPP

#include "../../Projeto IOCP/SOCKET/session.h"
#include "../TYPE/pangya_game_st.h"
#include "../TYPE/player_info.h"
#include "../TYPE/gm_info.hpp"

// Game Guard
#include "../TYPE/game_guard_type.hpp"

// !@ Teste
#include "../UTIL/block_exec_one_per_time.hpp"

namespace stdA {
    class player : public session {
        public:
            player(threadpool_base& _threapool);
            virtual ~player();

			virtual bool clear() override;

			virtual unsigned char getStateLogged() override;

			virtual uint32_t getUID() override;
			virtual uint32_t getCapability() override;
			virtual char* getNickname() override;
			virtual char* getID() override;

			// Exp for the player
			virtual void addExp(uint32_t _exp, bool _upt_on_game = false);

			// Add (Sum) Exp to the equipped Caddie
			virtual void addCaddieExp(uint32_t _exp);

			// Add (Sum) Exp to the equipped Mascot
			virtual void addMascotExp(uint32_t _exp);

			// Add Static Exp
			static void addExp(uint32_t _uid, uint32_t _exp);

			// Pang
			virtual void addPang(uint64_t _pang);
			virtual void consomePang(uint64_t _pang);

			// Cookie
			virtual void addCookie(uint64_t _cookie);
			virtual void consomeCookie(uint64_t _cookie);

			// Moedas
			virtual void addMoeda(uint64_t _pang, uint64_t _cookie);
			virtual void consomeMoeda(uint64_t _pang, uint64_t _cookie);

			// Saves the Log in the database of Spending "Now has CP Pouch (earned)" of CP (Cookie Point)
			virtual void saveCPLog(CPLog& _cp_log);

			// Saves the Log in the database of Spending "Now has CP Pouch (earned)" of CP (Cookie Point)
			static void saveCPLog(uint32_t _uid, CPLog& _cp_log);

			// Checks whether the Character's equipped items are valid
			bool checkCharacterEquipedPart(CharacterInfo& ci);
			bool checkCharacterEquipedAuxPart(CharacterInfo& ci);
			bool checkCharacterEquipedCutin(CharacterInfo& ci);
			void checkCharacterAllItemEquiped(CharacterInfo& ci);

			bool checkSkinEquiped(UserEquip& _ue);
			bool checkPosterEquiped(UserEquip& _ue);
			bool checkCharacterEquiped(UserEquip& _ue);
			bool checkCaddieEquiped(UserEquip& _ue);
			bool checkMascotEquiped(UserEquip& _ue);
			bool checkClubSetEquiped(UserEquip& _ue);
			bool checkBallEquiped(UserEquip& _ue);
			bool checkItemEquiped(UserEquip& _ue);
			void checkAllItemEquiped(UserEquip& _ue);

			void equipDefaultCharacter(UserEquip& _ue);
			void equipDefaultClubSet(UserEquip& _ue);
			void equipDefaultBall(UserEquip& _ue);
			void equipDefaultBallPremiumUser(UserEquip& _ue);

			// Checks whether an AuxPart is equipped on any of the player's Characters
			// Returns a vector with a pointer to each Character Info if it is equipped
			// Returns an empty vector if it is not equipped on any of the player's characters
			std::vector< CharacterInfo* > isAuxPartEquiped(uint32_t _typeid);

			// Checks whether a Part is equipped on any of the player's Characters
			// Returns a pointer to the Character Info if it is equipped
			// Returns nullptr if it is not equipped
			CharacterInfo* isPartEquiped(uint32_t _typeid);

		protected:
			static void SQLDBResponse(uint32_t _msg_id, pangya_db& _pangya_db, void* _arg);

        public:
            PlayerInfo m_pi;
			GMInfo m_gi;

			// Game Guard
			PlayerGameGuard m_gg;

#if STDA_BLOCK_PACKET_ONE_TIME_DISABLE != 0x1
			// Block Packet Exec One Per Time
			SyncBlockExecOnePerTime* m_sbeopt;
#endif
    };
}

#endif