// File rank_registry.hpp
// Created on 16/06/2020 at 14:48 by Acrisio
// Definition of the class RankRegistry

#pragma once
#ifndef _STDA_RANK_REGISTRY_HPP
#define _STDA_RANK_REGISTRY_HPP

#include "../TYPE/pangya_rank_st.hpp"

#include <string>
#include "../../Projeto IOCP/PACKET/packet.h"

// Used in the RankEntry typedef
#include <map>
#include <vector>

#define CHECK_LIMIT_RANK_POSITION_COMPACT_PACKET(__position) ((__position) > LIMIT_RANK_POSITION_TO_COMPACT_PACKET_SHOW)
#define CHECK_LIMIT_RANK_POSITION_COMPACT_PACKET_AND_SET(__position) ((CHECK_LIMIT_RANK_POSITION_COMPACT_PACKET((__position))) ? 0u : (__position))

namespace stdA {

	// Over 10 thousand, pangya zeroes it in the compact packet
	constexpr uint32_t LIMIT_RANK_POSITION_TO_COMPACT_PACKET_SHOW = 10000u;

	class RankRegistry {
		public:
			RankRegistry();
			RankRegistry(uint32_t _uid, uint32_t _current_position, uint32_t _last_position, int32_t _value);
			virtual ~RankRegistry();

			void clear();

			// Fill Packet with data from Object
			void toPacket(packet& _packet);

			void toCompactPacket(packet& _packet);

			// Get
			uint32_t& getUID();
			uint32_t& getCurrentPosition();
			uint32_t& getLastPosition();
			int32_t& getValue();

			// Set
			void setUID(uint32_t _uid);
			void setCurrentPosition(uint32_t _current_position);
			void setLastPosition(uint32_t _last_position);
			void setValue(int32_t _value);

		protected:
			uint32_t m_uid;
			uint32_t m_current_position;
			uint32_t m_last_position;
			int32_t m_value;						// Data

	};

	// Rank Entry Value
	typedef std::map< key_position, RankRegistry > RankEntryValue;

	// Rank Entry
	typedef std::map< key_menu, RankEntryValue > RankEntry;

	// Rank Entry Value Rank
	typedef std::pair< RankEntryValue::iterator /*Begin*/, RankEntryValue::iterator /*End*/> RankEntryValueRange;
}

#endif // !_STDA_RANK_REGISTRY_HPP
