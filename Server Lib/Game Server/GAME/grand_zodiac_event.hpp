// File grand_zodiac_event.hpp
// Created on 26/06/2020 at 13:09 by Acrisio
// Definition of class GrandZodiacEvent

#pragma once
#ifndef _STDA_GRAND_ZODIAC_EVENT_HPP
#define _STDA_GRAND_ZODIAC_EVENT_HPP

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>
#endif

#include "../../Projeto IOCP/TYPE/singleton.h"
#include "../TYPE/grand_zodiac_type.hpp"

#include <vector>

namespace stdA {
	class GrandZodiacEvent {

		public:
			GrandZodiacEvent();
			virtual ~GrandZodiacEvent();

			/*static */void load();

			/*static */bool isLoad();

			/*static */bool checkTimeToMakeRoom();

			// Checks whether the message of the current time interval has already been sent 
			/*static*/bool messageSended();

			// Zeroes all intervals that are not due, and sets the interval that is due
			/*static*/void setSendedMessage();

			/*static*/range_time* getInterval();

		protected:
			/*static */void initialize();

			/*static */void clear();

		private:
			/*static */std::vector< range_time > m_rt;	// Times to make room event

			/*static */bool m_load;

			SYSTEMTIME m_st;							// Used to avoid creating it directly in the check function
			
#if defined(_WIN32)
			CRITICAL_SECTION m_cs;
#elif defined(__linux__)
			pthread_mutex_t m_cs;
#endif
	};

	typedef Singleton< GrandZodiacEvent > sGrandZodiacEvent;
}

#endif // !_STDA_GRAND_ZODIAC_EVENT_HPP
