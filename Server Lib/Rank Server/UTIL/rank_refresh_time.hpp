// File rank_refresh_time.hpp
// Created on 18/06/2020 at 22:04 by Acrisio
// Definition of the class rank_refresh_time

#pragma once
#ifndef _STDA_RANK_REFRESH_TIME_HPP
#define _STDA_RANK_REFRESH_TIME_HPP

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#endif

#include <string>

namespace stdA {
	class rank_refresh_time {
		public:
			rank_refresh_time();
			rank_refresh_time(uint32_t _interval, std::string _date);
			rank_refresh_time(uint32_t _interval, SYSTEMTIME& _st);
			rank_refresh_time(uint32_t _interval, FILETIME& _ft);
			virtual ~rank_refresh_time();

			void clear();

			// Past the date, expired, can update the Rank records
			bool isOutDated();

			std::string toString();

			// Get
			uint32_t& getIntervalRefresh();

			SYSTEMTIME& getLastRefreshDateSystemTime();
			FILETIME& getLastRefreshDateFileTime();

			// Set
			void setIntervalRefresh(uint32_t _interval);

			void setLastRefreshDate(std::string _date);
			void setLastRefreshDate(SYSTEMTIME& _st);
			void setLastRefreshDate(FILETIME& _ft);

		protected:
			uint32_t m_interval_refresh;			// Interval in hours

			SYSTEMTIME m_st_last_date;					// System Time - Last date the rank records were updated
			FILETIME m_ft_last_date;					// File Time - Last date the rank records were updated

	};
}

#endif // !_STDA_RANK_REFRESH_TIME_HPP
