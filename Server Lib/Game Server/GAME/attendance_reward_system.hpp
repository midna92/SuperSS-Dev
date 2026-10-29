// File attendance_reward_system.hpp
// Created on 14/11/2018 at 21:22 by Acrisio
// Definition of class AttendanceRewardSystem

#pragma once
#ifndef _STDA_ATTENDANCE_REWARD_SYSTEM_HPP
#define _STDA_ATTENDANCE_REWARD_SYSTEM_HPP

#if defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>
#endif

#include "../SESSION/player.hpp"
#include "../../Projeto IOCP/PACKET/packet.h"
#include "../../Projeto IOCP/PANGYA_DB/pangya_db.h"
#include "../../Projeto IOCP/TYPE/singleton.h"

#include <vector>

namespace stdA {
	class AttendanceRewardSystem {
		public:
			AttendanceRewardSystem();
			virtual ~AttendanceRewardSystem();

			/*static*/ void load();

			/*static*/ bool isLoad();

			/*static*/ void requestCheckAttendance(player& _session, packet *_packet);
			/*static*/ void requestUpdateCountLogin(player& _session, packet *_packet);

		protected:
			/*static*/ void initialize();

			/*static*/ void clear();

			// Gives 3 Grand Prix Tickets to the player for having logged in for the first time today, but only gives if they have not reached the grand prix ticket limit
			/*static*/ void sendGrandPrixTicket(player& _session);

			/*static*/ AttendanceRewardItemCtx* drawReward(unsigned char _tipo);

			// Checks whether a day has passed since the player logged into pangya
			/*static*/ bool passedOneDay(player& _session);

			static void SQLDBResponse(uint32_t _msg_id, pangya_db& _pangya_db, void* _arg);

		private:
			/*static*/ std::vector< AttendanceRewardItemCtx > v_item;

			/*static*/ bool m_load;

#if defined(_WIN32)
			CRITICAL_SECTION m_cs;
#elif defined(__linux__)
			pthread_mutex_t m_cs;
#endif
	};

	typedef Singleton< AttendanceRewardSystem > sAttendanceRewardSystem;
}

#endif // !_STDA_ATTENDANCE_REWARD_SYSTEM_HPP
