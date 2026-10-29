// File special_shuffle_course.hpp
// Created on 16/10/2018 at 22:03 by Acrisio
// Definition of the SpecialShuffleCourse class

#pragma once
#ifndef _STDA_SPECIAL_SHUFFLE_COURSE_HPP
#define _STDA_SPECIAL_SHUFFLE_COURSE_HPP

#include "tourney_base.hpp"

namespace stdA {
	class SpecialShuffleCourse : public TourneyBase {
		public:
			SpecialShuffleCourse(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie);
			virtual ~SpecialShuffleCourse();

			virtual bool deletePlayer(player* _session, int _option) override;

			virtual void deleteAllPlayer();

			// Game->Course->Hole methods
			virtual void changeHole(player& _session) override;
			virtual void finishHole(player& _session) override;

			void finish_SSC(player& _session, int _option);

			// Time
			/*virtual void startTime() override;
			virtual void stopTime();
			virtual void pauseTime();
			virtual void resumeTime();*/
			virtual void timeIsOver() override;

		protected:
			// Initialize Game and Finish Game
			virtual bool init_game() override;

			virtual void finish();

			virtual DropItemRet requestInitDrop(player& _session) override;

			virtual void requestUpdateItemUsedGame(player& _session) override;

			virtual void requestFinishData(player& _session);

			virtual void requestDrawTreasureHunterItem(player& _session);

			virtual void requestMakeMasterCoin();

			virtual void requestSendMasterCoin(player& _session);

		public:
			virtual bool finish_game(player& _session, int option = 0) override;

		protected:
			bool m_SSC_state;

			uint32_t m_coin_SSC;		// That the room master wins if he stays until the end

	};
}

#endif // !_STDA_SPECIAL_SHUFFLE_COURSE_HPP
