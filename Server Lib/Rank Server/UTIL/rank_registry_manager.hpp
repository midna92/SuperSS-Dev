// File rank_registry_manager.hpp
// Created on 16/06/2020 at 15:17 by Acrisio
// Definition of the class RankRegistryManager

#pragma once
#ifndef _STDA_RANK_REGISTRY_MANAGER_HPP
#define _STDA_RANK_REGISTRY_MANAGER_HPP

#if defined(__linux__)
#include <pthread.h>
#include <unistd.h>
#endif

#include "rank_registry.hpp"
#include "rank_character.hpp"

#include "../../Projeto IOCP/PACKET/packet.h"
#include "../SESSION/player.hpp"

#include <fstream>

namespace stdA {

	// Limit of records per page
	constexpr uint32_t LIMIT_REGISTRY_FOR_PAGE = 12u;

	// Found Player typedef
	typedef std::pair< uint32_t /*Player position*/, int32_t /*Page or -1 error*/ > FoundPlayer;

	class RankRegistryManager {
		public:
			RankRegistryManager();
			virtual ~RankRegistryManager();

			void load();

			bool isLoad();

			// Put the page the player requested in the packet if it exists
			void pageToPacket(packet& _packet, search_dados& _sd);

			// Put the player's position and value in the packet if present
			void playerPositionToPacket(packet& _packet, player& _session, search_dados& _sd);

			// Send the player's complete info with character info and the complete overall
			void sendPlayerFullInfo(player& _session, uint32_t _uid/*Player Info*/);

			// Send the page where the searched player was found
			void sendPageFoundPlayer(player& _session, FoundPlayer& _fp, search_dados& _sd);

			// Search for a player by nickname and send the page where they are if they are in the rank
			void searchPlayerByNicknameAndSendPage(player& _session, std::string _nickname, search_dados& _sd);

			// Search for a player by position and send the page where they are if they are in the rank
			void searchPlayerByRankAndSendPage(player& _session, uint32_t _position, search_dados& _sd);

			// Search for a player by nickname in the Rank Menu->Item
			FoundPlayer searchPlayerByNickname(std::string _nickname, search_dados& _sd);

			// Search for a player by their rank in the Rank Menu->Item
			FoundPlayer searchPlayerByRank(uint32_t _position, search_dados& _sd);

			// Create a log of all the records in a file with the date
			void makeLog();

		protected:
			void initialize();

			void clear();

			std::map< eRANK_OVERALL, RankRegistry > getAllOverallInfoFromPlayer(uint32_t _uid);

			std::pair< RankEntryValueRange, bool > getPage(RankEntry::iterator _registrys, unsigned int & _page);

		protected:
			std::ofstream log;

			std::string prex;
			std::string dir;

			void init_log();
			void close_log();

			inline void putLog(std::string _str_log);

		protected:
			RankEntry m_entry;
			RankCharacterEntry m_character_entry;

			bool m_state;

#if defined(_WIN32)
			CRITICAL_SECTION m_cs;
#elif defined(__linux__)
			pthread_mutex_t m_cs;
#endif
	};
}

#endif // !_STDA_RANK_REGISTRY_MANAGER_HPP
