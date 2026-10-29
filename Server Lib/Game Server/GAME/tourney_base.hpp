// File tourney_base.hpp
// Created on 18/08/2018 at 15:00 by Acrisio
// Definition of the TourneyBase class

#pragma once
#ifndef _STDA_TOURNEY_BASE_HPP
#define _STDA_TOURNEY_BASE_HPP

#include "game.hpp"
#include "../TYPE/tourney_base_type.hpp"

namespace stdA {
	class TourneyBase : public Game {
		public:
			TourneyBase(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie);
			virtual ~TourneyBase();

			virtual void sendInitialData(player& _session) override;
			virtual void sendInitialDataAfter(player& _session) override;

			// Game->Course->Hole methods
			virtual void requestInitHole(player& _session, packet *_packet) override;
			virtual bool requestFinishLoadHole(player& _session, packet *_packet) override;
			virtual void requestFinishCharIntro(player& _session, packet *_packet) override;
			virtual void requestFinishHoleData(player& _session, packet *_packet) override;

			virtual void changeHole(player& _session) = 0;
			virtual void finishHole(player& _session) = 0;

			virtual void requestInitShot(player& _session, packet *_packet) override;
			virtual void requestSyncShot(player& _session, packet *_packet) override;
			virtual void requestInitShotArrowSeq(player& _session, packet *_packet) override;
			virtual void requestShotEndData(player& _session, packet *_packet) override;
			virtual RetFinishShot requestFinishShot(player& _session, packet *_packet) override;

			virtual void requestChangeMira(player& _session, packet *_packet) override;
			virtual void requestChangeStateBarSpace(player& _session, packet *_packet) override;
			virtual void requestActivePowerShot(player& _session, packet *_packet) override;
			virtual void requestChangeClub(player& _session, packet  *_packet) override;
			virtual void requestUseActiveItem(player& _session, packet *_packet) override;
			virtual void requestChangeStateTypeing(player& _session, packet *_packet) override;	// Typing
			virtual void requestMoveBall(player& _session, packet *_packet) override;
			virtual void requestChangeStateChatBlock(player& _session, packet *_packet) override;
			virtual void requestActiveBooster(player& _session, packet *_packet) override;
			virtual void requestActiveReplay(player& _session, packet *_packet) override;
			virtual void requestActiveCutin(player& _session, packet *_packet) override;

			// Ability Item
			virtual void requestActiveRing(player& _session, packet *_packet) override;
			virtual void requestActiveRingGround(player& _session, packet *_packet) override;
			virtual void requestActiveRingPawsRainbowJP(player& _session, packet *_packet) override;
			virtual void requestActiveRingPawsRingSetJP(player& _session, packet *_packet) override;
			virtual void requestActiveRingPowerGagueJP(player& _session, packet *_packet) override;
			virtual void requestActiveRingMiracleSignJP(player& _session, packet *_packet) override;
			virtual void requestActiveWing(player& _session, packet *_packet) override;
			virtual void requestActivePaws(player& _session, packet *_packet) override;
			virtual void requestActiveGlove(player& _session, packet *_packet) override;
			virtual void requestActiveEarcuff(player& _session, packet *_packet) override;

			virtual void requestUpdateTrofel() override;

			// Exclusive to Tourney Mode
			virtual void requestSendTimeGame(player& _session) override;
			virtual void requestUpdateEnterAfterStartedInfo(player& _session, EnterAfterStartInfo& _easi) override;

			// Game
			virtual bool requestFinishGame(player& _session, packet *_packet) override;

			// Time
			virtual void startTime();
			/*virtual bool stopTime();
			virtual bool pauseTime();
			virtual bool resumeTime();*/
			virtual void timeIsOver() = 0;

		protected:
			// Initialize Game and Finish Game
			virtual bool init_game() override = 0;

			// Request Trate Shot Sync Data
			virtual void requestTranslateSyncShotData(player& _session, ShotSyncData& _ssd) override;
			virtual void requestReplySyncShotData(player& _session) override;

			virtual void sendRemainTime(player& _session);

			// Finished Hole Update on Game
			// Opt 0 Did not finish the hole, 1 Finished the Hole
			virtual void updateFinishHole(player& _session, int _option);

			// Updates the Treasure Hunter Point in the Game
			virtual void updateTreasureHunterPoint(player& _session);

			// Draws the Player's Treasure Hunter Item
			virtual void requestDrawTreasureHunterItem(player& _session);

			// Sync Shot Player
			virtual void sendSyncShot(player& _session);

			// Sends the packet that finishes the shot,
			// and if there is a captured item such as coin and spinning cube, sends it in this packet
			virtual void sendEndShot(player& _session, DropItemRet& _cube);

			// Sends response that the player finished the game or left
			// Update the player's Score Board
			// Opt 2 Finished, 3 Left
			virtual void sendUpdateState(player& _session, int _option);

			// Sends Drop Item that the player won in the Tourney
			virtual void sendDropItem(player& _session);

			// Sends the Game Scoreboard, the Final Scores, pang and experience, etc
			virtual void sendPlacar(player& _session);

			// Sends the drawn Treasure Hunter Item, interface only (Visual)
			virtual void sendTreasureHunterItemDrawGUI(player& _session);

			// Sends to the player that the Tourney time is over
			virtual void sendTimeIsOver(player& _session);

			// Checks if it is the last shot of the hole, to move on to the next hole
			virtual int checkEndShotOfHole(player& _session);

			// Draws Item Drop of the Hole, may or may not drop
			virtual void drawDropItem(player& _session);

			// Achievement Top 3 player achievement
			virtual void achievement_top_3_1st(player& _session);

			// Calcula Shot to Spinning Cube
			virtual void calcule_shot_to_spinning_cube(player& _session, ShotSyncData& _ssd);

			// Calcula Shot to Coin
			virtual void calcule_shot_to_coin(player& _session, ShotSyncData& _ssd);

			// request calculate shot spinning cube - It is implemented in the classes that will use it
			virtual void requestCalculeShotSpinningCube(player& _session, ShotSyncData& _ssd);

			// request calculate shot coin - It is implemented in the classes that will use it
			virtual void requestCalculeShotCoin(player& _session, ShotSyncData& _ssd);

		public:
			virtual bool finish_game(player& _session, int option = 0) override = 0;

		protected:
			static int end_time(void* _arg1, void* _arg2);

		protected:
			uint32_t m_max_player;
			int32_t m_entra_depois_flag;

			TicketReportInfo m_tri;

			Medal m_medal[12];
	};
}

#endif // !_STDA_TOURNEY_BASE_HPP
