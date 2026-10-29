// File pratice.hpp
// Created on 18/08/2018 at 15:23 by Acrisio
// Definition of the Practice class

#pragma once
#ifndef _STDA_PRACTICE_HPP
#define _STDA_PRACTICE_HPP

#include "tourney_base.hpp"

namespace stdA {
	class Practice : public TourneyBase {
		public:
			Practice(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie);
			virtual ~Practice();

			// Methods of Game->Course->Hole
			virtual void changeHole(player& _session) override;
			virtual void finishHole(player& _session) override;

			virtual void requestInitHole(player& _session, packet *_packet) override;

			virtual void requestCalculePang(player& _session) override;

			void finish_practice(player& _session, int _option);

			virtual void requestChangeWindNextHoleRepeat(player& _session, packet *_packet) override;

			// Time
			/*virtual void startTime();
			virtual void stopTime();
			virtual void pauseTime();
			virtual void resumeTime();*/
			virtual void timeIsOver() override;

		protected:
			// Initializes Game and Finishes Game
			virtual bool init_game() override;

			virtual void requestReplySyncShotData(player& _session) override;

			virtual void requestSavePang(player& _session);

			virtual void requestFinishExpGame();

			virtual void finish();

			virtual void requestFinishData(player& _session);

			// Checks whether it is the last shot of the hole, to move on to the next hole
			virtual int checkEndShotOfHole(player& _session);

			// request calculate shot spinning cube - It is implemented in the classes that will use it
			virtual void requestCalculeShotSpinningCube(player& _session, ShotSyncData& _ssd) override;

			// request calculate shot coin - It is implemented in the classes that will use it
			virtual void requestCalculeShotCoin(player& _session, ShotSyncData& _ssd) override;

		public:
			virtual bool finish_game(player& _session, int option = 0) override;

		protected:
			bool m_practice_state;
	};
}

#endif // !_STDA_PRACTICE_HPP