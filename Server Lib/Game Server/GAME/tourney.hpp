// File tourney.hpp
// Created on 22/09/2018 at 12:06 by Acrisio
// Definition of the Tourney class

#pragma once
#ifndef _STDA_TOURNEY_HPP
#define _STDA_TOURNEY_HPP

#include "tourney_base.hpp"

namespace stdA {
	class Tourney : public TourneyBase {
		public:
			Tourney(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie);
			virtual ~Tourney();

			virtual bool deletePlayer(player* _session, int _option) override;

			virtual void deleteAllPlayer();

			// Game->Course->Hole methods
			virtual bool requestFinishLoadHole(player& _session, packet *_packet) override;

			virtual void changeHole(player& _session) override;
			virtual void finishHole(player& _session) override;

			void finish_tourney(player& _session, int _option);

			virtual bool requestUseTicketReport(player& _session, packet *_packet) override;

			virtual void requestStartAfterEnter(job& _job) override;
			virtual void requestEndAfterEnter() override;

			// Time
			/*virtual void startTime() override;
			virtual void stopTime();
			virtual void pauseTime();
			virtual void resumeTime();*/
			virtual void timeIsOver() override;

		protected:
			// Initialize Game and Finish Game
			virtual bool init_game() override;

			virtual void clear_time_after_enter();

			virtual void requestFinishExpGame();

			virtual void finish();

			virtual void requestMakeMedal();

			virtual void requestMakeTrofel();

			virtual void requestSaveTicketReport();

			virtual void requestSendTicketReport();

			virtual void requestGiveMedalAndItens();

			virtual void requestFinishData(player& _session);

			// request calculate shot spinning cube - It is implemented in the classes that will use it
			virtual void requestCalculeShotSpinningCube(player& _session, ShotSyncData& _ssd) override;

			// request calculate shot coin - It is implemented in the classes that will use it
			virtual void requestCalculeShotCoin(player& _session, ShotSyncData& _ssd) override;

		protected:
			static bool speediest_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2);
			static bool best_drive_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2);
			static bool best_chipin_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2);
			static bool best_long_puttin_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2);
			static bool best_recovery(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2);

		public:
			virtual bool finish_game(player& _session, int option = 0) override;

		protected:
			timer *m_pTimer_after_enter;		// Timer to enter Tourney later

			bool m_tourney_state;
	};
}

#endif // !_STDA_TOURNEY_HPP
