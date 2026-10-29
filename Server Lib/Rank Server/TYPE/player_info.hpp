// File player_info.hpp
// Created on 15/06/2020 at 15:07 by Acrisio
// Definition of the class PlayerInfo

#pragma once
#ifndef _STDA_PLAYER_INFO_HPP
#define _STDA_PLAYER_INFO_HPP

#include "pangya_rank_st.hpp"

namespace stdA {
	class PlayerInfo : public player_info {
		public:
			PlayerInfo();
			virtual ~PlayerInfo();

			virtual void clear();

		public:
			unsigned char m_state;

			// Data used to query the rank
			search_dados_ex m_sd;			// Search data
	};
}

#endif // !_STDA_PLAYER_INFO_HPP
