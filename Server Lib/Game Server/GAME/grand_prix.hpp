// File grand_prix.hpp
// Created on 18/06/2019 at 08:22 by Acrisio
// Definition of class GrandPrix

#pragma once
#ifndef _STDA_GRAND_PRIX_HPP
#define _STDA_GRAND_PRIX_HPP

#if defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>
#endif

#include "tourney_base.hpp"
#include "../../Projeto IOCP/THREAD POOL/thread.h"
#include "../TYPE/grand_prix_type.hpp"

namespace stdA {
	class GrandPrix : public TourneyBase {
		public:
			GrandPrix(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie, IFF::GrandPrixData& _gp);
			virtual ~GrandPrix();

			virtual void sendInitialData(player& _session) override;

			virtual bool deletePlayer(player* _session, int _option) override;

			virtual void deleteAllPlayer();

			// Methods of Game->Course->Hole
			virtual void requestFinishCharIntro(player& _session, packet *_packet) override;
			virtual void requestActiveBooster(player& _session, packet *_packet) override;

			virtual void requestStartTurnTime(player& _session, packet *_packet) override;

			virtual void changeHole(player& _session) override;
			virtual void finishHole(player& _session) override;

			virtual void requestInitShot(player& _session, packet *_packet) override;
			//virtual void requestSyncShot(player& _session, packet *_packet) override;
			//virtual RetFinishShot requestFinishShot(player& _session, packet *_packet) override;

			void finish_grand_prix(player& _session, int _option);

			// Time
			virtual void startTime(void* _quem);
			virtual bool stopTime(void* _quem);
			virtual void timeIsOver(void* _quem);

			// abstract timeIsOver of class TourneyBase
			virtual void timeIsOver() override { return; };

		protected:

			// Calculates Grand Prix Rank Place
			virtual void requestCalculeRankPlace();

			// Initializes Game and Finishes Game
			virtual bool init_game() override;

			virtual int checkEndShotOfHole(player& _session) override;

			virtual void requestTranslateSyncShotData(player& _session, ShotSyncData& _ssd) override;

			// Initializes the Grand Prix Bots if there is room for them
			virtual void init_bots();

			// Consumes the ticket of the players who will play the Grand Prix
			virtual void consomeTicket();

			virtual void requestFinishExpGame();

			virtual void requestMakeRankPlayerDisplayCharacter();

			virtual void finish();

			virtual void requestFinishData(player& _session);

			virtual void requestSaveGrandPrixClear(player& _session);

			// Sends
			virtual void sendTrofel(player& _session);

			virtual void sendRankPlayerDisplayCharacter(player& _session);

			virtual void sendRewardRankAndGrandPrix(player& _session);

			virtual void sendAllToNextHole();

			virtual int changeTurn(player& _session);

			// Sync
			virtual bool checkAllShotPacket(player& _session);
			virtual void clearAllShotPacket(player& _session);

			// Check and clear if true
			virtual bool checkAllClearHole();

			virtual void setClearHole(PlayerGameInfo* _pgi);

			virtual void clearAllClearHole();

			// Check and clear if true
			virtual bool checkAllClearHoleAndClear();

			virtual void clear_all_clear_hole();

			// Clears the times created in the Grand Prix
			virtual void clear_timers();

			// request calculate shot spinning cube - It is implemented in the classes that will use it
			virtual void requestCalculeShotSpinningCube(player& _session, ShotSyncData& _ssd) override;

			// request calculate shot coin - It is implemented in the classes that will use it
			virtual void requestCalculeShotCoin(player& _session, ShotSyncData& _ssd) override;

		public:
			virtual bool finish_game(player& _session, int option = 0) override;

		protected:
			static int end_time(void* _arg1, void* _arg2);

		protected:
			static int end_time_rule(void* _arg1, void* _arg2);

			// Rule Time
			virtual void startTimeRule(void* _quem);
			virtual bool stopTimeRule(void* _quem);
			virtual void timeRuleIsOver(void* _quem);

		protected:
			IFF::GrandPrixData m_gp;
			std::vector< IFF::GrandPrixRankReward > m_gp_reward;
			std::vector< Bot > m_bot;

			std::vector< RankPlayerDisplayChracter > m_rank_player_display_char;

			TimerManager m_timer_manager;
			TimerManager m_timer_manager_rule;

			LockManager m_lock_manager;

		protected:
			bool m_grand_prix_state;

#if defined(_WIN32)
			CRITICAL_SECTION m_cs_sync_shot;
#elif defined(__linux__)
			pthread_mutex_t m_cs_sync_shot;
#endif
	};
}

#endif // !_STDA_GRAND_PRIX_HPP
