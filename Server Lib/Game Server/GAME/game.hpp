// Arquivo game.hpp
// Created on 11/08/2018 at 22:21 by Acrisio
// Definição da classe base Game

#pragma once
#ifndef _STDA_GAME_HPP
#define _STDA_GAME_HPP

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>
#endif

#include "course.hpp"
#include "../TYPE/pangya_game_st.h"
#include "../SESSION/player.hpp"
#include <vector>
#include <map>

#include "../TYPE/game_type.hpp"
#include "../../Projeto IOCP/TYPE/smart_calculator_type.hpp"

#include "../../Projeto IOCP/TIMER/timer.h"

#define DECRYPT16(_buffer, _size, _key) { \
	if ((int)(_size) > 0 && (_buffer) != nullptr && (_key) != nullptr) \
		for (auto i = 0u; i < (_size); ++i) \
			(_buffer)[i] ^= (_key)[i % 16]; \
} \

namespace stdA {
	class Game {
		public:
			Game(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie);
			virtual ~Game();

			virtual void sendInitialData(player& _session);

			// Send the initial data to whoever joins the Game later
			virtual void sendInitialDataAfter(player& _session);

		protected:
			player* findSessionByOID(uint32_t _oid);
			player* findSessionByUID(uint32_t _uid);
			player* findSessionByNickname(std::string _nickname);
			player* findSessionByPlayerGameInfo(PlayerGameInfo *_pgi);

		public:
			PlayerGameInfo* getPlayerInfo(player *_session);

			// If _session is different from nullptr returns all sessions, except the one passed in _session
			std::vector< player* > getSessions(player *_session = nullptr);

			virtual SYSTEMTIME& getTimeStart();

			virtual void addPlayer(player& _session);
			virtual bool deletePlayer(player* _session, int _option);

			// Methods of Game->Course->Hole
			virtual void requestInitHole(player& _session, packet *_packet) = 0;
			virtual bool requestFinishLoadHole(player& _session, packet *_packet) = 0;
			virtual void requestFinishCharIntro(player& _session, packet *_packet) = 0;
			virtual void requestFinishHoleData(player& _session, packet *_packet) = 0;

			// They are implemented in their base class

			// These 2 here are from VS mode
			//virtual void changeHole() = 0;
			//virtual void finishHole() = 0;

			// These 2 here are from Tourney Mode
			//virtual void changeHole(player& _session) = 0;
			//virtual void finishHole(player& _session) = 0;

			// Server sent the InitShot reply to the client
			// This one is exclusive to VersusBase
			virtual void requestInitShotSended(player& _session, packet *_packet);

			virtual void requestInitShot(player& _session, packet *_packet) = 0;
			virtual void requestSyncShot(player& _session, packet *_packet) = 0;
			virtual void requestInitShotArrowSeq(player& _session, packet *_packet) = 0;
			virtual void requestShotEndData(player& _session, packet *_packet) = 0;
			virtual RetFinishShot requestFinishShot(player& _session, packet *_packet) = 0;

			virtual void requestChangeMira(player& _session, packet *_packet) = 0;
			virtual void requestChangeStateBarSpace(player& _session, packet *_packet) = 0;
			virtual void requestActivePowerShot(player& _session, packet *_packet) = 0;
			virtual void requestChangeClub(player& _session, packet  *_packet) = 0;
			virtual void requestUseActiveItem(player& _session, packet *_packet) = 0;
			virtual void requestChangeStateTypeing(player& _session, packet *_packet) = 0;	// Escrevendo
			virtual void requestMoveBall(player& _session, packet *_packet) = 0;
			virtual void requestChangeStateChatBlock(player& _session, packet *_packet) = 0;
			virtual void requestActiveBooster(player& _session, packet *_packet) = 0;
			virtual void requestActiveReplay(player& _session, packet *_packet) = 0;
			virtual void requestActiveCutin(player& _session, packet *_packet) = 0;

			// Hability Item
			virtual void requestActiveRing(player& _session, packet *_packet) = 0;
			virtual void requestActiveRingGround(player& _session, packet *_packet) = 0;
			virtual void requestActiveRingPawsRainbowJP(player& _session, packet *_packet) = 0;
			virtual void requestActiveRingPawsRingSetJP(player& _session, packet *_packet) = 0;
			virtual void requestActiveRingPowerGagueJP(player& _session, packet *_packet) = 0;
			virtual void requestActiveRingMiracleSignJP(player& _session, packet *_packet) = 0;
			virtual void requestActiveWing(player& _session, packet *_packet) = 0;
			virtual void requestActivePaws(player& _session, packet *_packet) = 0;
			virtual void requestActiveGlove(player& _session, packet *_packet) = 0;
			virtual void requestActiveEarcuff(player& _session, packet *_packet) = 0;
			
			virtual void requestActiveAutoCommand(player& _session, packet *_packet);	// Auto Comando [Especial Shot Help]
			virtual void requestActiveAssistGreen(player& _session, packet *_packet);	// Magic eye

			// This one exists only in VersusBase and its derivatives
			virtual void requestMarkerOnCourse(player& _session, packet *_packet);
			virtual void requestLoadGamePercent(player& _session, packet *_packet);
			virtual void requestStartTurnTime(player& _session, packet *_packet);
			virtual void requestUnOrPause(player& _session, packet *_packet);

			// Common Command GM Change Wind Versus
			virtual void requestExecCCGChangeWind(player& _session, packet *_packet);
			virtual void requestExecCCGChangeWeather(player& _session, packet *_packet);

			// Continue the versus after the player left from hole 3 onwards and if the game is 18h
			virtual void requestReplyContinue();

			// This one exists only in TourneyBase and its derivatives
			virtual bool requestUseTicketReport(player& _session, packet *_packet);

			// It is only implemented in Practice
			virtual void requestChangeWindNextHoleRepeat(player& _session, packet *_packet);

			// Exclusive to Tourney Mode
			virtual void requestStartAfterEnter(job& _job);
			virtual void requestEndAfterEnter();
			virtual void requestUpdateTrofel();

			// Exclusive to Match Mode
			virtual void requestTeamFinishHole(player& _session, packet *_packet);

			// Game
			virtual bool requestFinishGame(player& _session, packet *_packet) = 0;

			// Request the Hole the player is on, 
			// if they are playing or 0 if he is not playing
			virtual unsigned char requestPlace(player& _session);

			// Check if the player has already been in the room
			virtual bool isGamingBefore(uint32_t _uid);

			// Exclusive to Tourney Mode
			virtual void requestSendTimeGame(player& _session);
			virtual void requestUpdateEnterAfterStartedInfo(player& _session, EnterAfterStartInfo& _easi);

			// Exclusive to Grand Zodiac Mode
			virtual void requestStartFirstHoleGrandZodiac(player& _session, packet *_packet);
			virtual void requestReplyInitialValueGrandZodiac(player& _session, packet *_packet);

			virtual void requestReadSyncShotData(player& _session, packet *_packet, ShotSyncData& _ssd);

			// Smart Calculator Command
			virtual bool execSmartCalculatorCmd(player& _session, std::string& _msg, eTYPE_CALCULATOR_CMD _type);

			virtual stGameShotValue getGameShotValueToSmartCalculator(player& _session, unsigned char _club_index, unsigned char _power_shot_index);

			// Tempo
			// Start Time Has its definitions
			//virtual void startTime() = 0;

			virtual bool stopTime();

			virtual bool pauseTime();
			virtual bool resumeTime();
			
			// time is over has its definitions
			//virtual void timeIsOver() = 0;

			// Report Game
			virtual void requestPlayerReportChatGame(player& _session, packet *_packet);

		protected:
			virtual void initPlayersItemRainRate();
			virtual void initPlayersItemRainPersistNextHole();
			virtual void initArtefact();

			virtual PlayerGameInfo::eCARD_WIND_FLAG getPlayerWindFlag(player& _session);
			virtual int initCardWindPlayer(PlayerGameInfo* _pgi, unsigned char _wind);

			virtual PlayerGameInfo::stTreasureHunterInfo getPlayerTreasureInfo(player& _session);

			virtual void updatePlayerAssist(player& _session);

			virtual void initGameTime();

			virtual uint32_t getRankPlace(player& _session);

			virtual DropItemRet requestInitDrop(player& _session);

			virtual void requestSaveDrop(player& _session);

			virtual DropItemRet requestInitCubeCoin(player& _session, packet *_packet);
			
			virtual void requestCalculePang(player& _session);

			virtual void requestSaveInfo(player& _session, int option);

			virtual void requestUpdateItemUsedGame(player& _session);

			virtual void requestFinishItemUsedGame(player& _session);

			virtual void requestFinishHole(player& _session, int option);

			virtual void requestSaveRecordCourse(player& _session, int game, int option);

			virtual void requestInitItemUsedGame(player& _session, PlayerGameInfo& _pgi);

			virtual void requestSendTreasureHunterItem(player& _session);

			virtual unsigned char checkCharMotionItem(player& _session);

			// Update the user Info, Trophy Info and Course Map Statistics
			// Opt 0 Sends everything, -1 does not send the map statistics
			virtual void sendUpdateInfoAndMapStatistics(player& _session, int _option);

			// Send the chat message to all Game players that the player finished the game
			virtual void sendFinishMessage(player& _session);

			virtual void requestCalculeRankPlace();

			// Set Flag Game and finish_game flag
			virtual void setGameFlag(PlayerGameInfo* _pgi, PlayerGameInfo::eFLAG_GAME _fg);
			virtual void setFinishGameFlag(PlayerGameInfo* _pgi, unsigned char _finish_game);

			// Check And Clear
			virtual bool AllCompleteGameAndClear();
			virtual bool PlayersCompleteGameAndClear();

			// Check if it is the last hole done
			virtual bool checkEndGame(player& _session);

			// Return all players who entered the game, except those who quit
			virtual uint32_t getCountPlayersGame();

			// Inicializa Jogo e Finaliza Jogo
			virtual bool init_game() = 0;

			// Trata Shot Sync Data
			virtual void requestTranslateSyncShotData(player& _session, ShotSyncData& _ssd) = 0;
			virtual void requestReplySyncShotData(player& _session) = 0;

			virtual void clear_time();

			virtual void clear_player_order();

			// Achievement
			virtual void initAchievement(player& _session);
			virtual void records_player_achievement(player& _session);
			virtual void update_sync_shot_achievement(player& _session, Location& _last_location);
			virtual void rain_hole_consecutivos_count(player& _session);
			virtual void score_consecutivos_count(player& _session);	// 2 or more consecutive holes
			virtual void rain_count(player& _session);

			// Effect Active in Shot Player
			virtual void setEffectActiveInShot(player& _session, uint64_t _effect);

			// Clear the data used for each shot, reset it to use in the next shot
			virtual void clearDataEndShot(PlayerGameInfo* _pgi);

			virtual void checkEffectItemAndSet(player& _session, uint32_t _typeid);

		public:
			virtual bool finish_game(player& _session, int option = 0) = 0;

		protected:
			static void SQLDBResponse(uint32_t _msg_id, pangya_db& _pangya_db, void* _arg);
			static bool sort_player_rank(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2);

		protected:
			std::vector< player* > m_players;

			std::map< player*, PlayerGameInfo* > m_player_info;		// This one is not deleted when the player leaves the game, the game needs this data to show later

			std::vector< PlayerGameInfo* > m_player_order;			// List of players of the game rank

			std::map< uint32_t/*UID*/, uint32_t/*UID*/ > m_player_report_game;		// UID of the player who reported the game

			void makePlayerInfo(player& _session);

			void clearAllPlayerInfo();

			virtual void initAllPlayerInfo();

			// Make Object Player Info Polimofirsmo
			virtual PlayerGameInfo* makePlayerInfoObject(player& _session);

#if defined(_WIN32)
			CRITICAL_SECTION m_cs;

			CRITICAL_SECTION m_cs_sync_finish_game;
#elif defined(__linux__)
			pthread_mutex_t m_cs;

			pthread_mutex_t m_cs_sync_finish_game;
#endif

		protected:
			RoomInfoEx& m_ri;
			RateValue m_rv;

			int m_game_init_state;				// game state, 1 started, 2 finished, -1 not even started
			bool m_state;						// Class state

			SYSTEMTIME m_start_time;
			timer *m_timer;						// Pointer to the timer class object

			unsigned char m_channel_rookie;

			volatile uint32_t m_sync_send_init_data;		// Value to Synchronize the initial Send Data of the game

			Course *m_course;	// Must stay last, because the constructor has to initialize the other variables first
	};
}

#endif // !_STDA_GAME_HPP
