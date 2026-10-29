// File versus.cpp
// Created on 20/10/2018 at 22:49 by Acrisio
// Implementation of the Versus class

#if defined(_WIN32)
#pragma pack(1)
#endif

#if defined(_WIN32)
#include <WinSock2.h>
#endif

#include "versus.hpp"
#include "../../Projeto IOCP/UTIL/exception.h"
#include "../../Projeto IOCP/TYPE/stda_error.h"
#include "../../Projeto IOCP/UTIL/message_pool.h"

#include "../PACKET/packet_func_sv.h"

#include "treasure_hunter_system.hpp"

#include "../../Projeto IOCP/DATABASE/normal_manager_db.hpp"

#include "../PANGYA_DB/cmd_update_last_player_game.hpp"

#define CHECK_SESSION_BEGIN(method) if (!_session.getState()) \
										throw exception("[Versus::request" + std::string((method)) +"][Error] player nao esta connectado.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::VERSUS_BASE, 1, 0)); \

#define REQUEST_BEGIN(method) CHECK_SESSION_BEGIN(std::string("request") + (method)) \
							  if (_packet == nullptr) \
									throw exception("[Versus::request" + std::string((method)) +"][Error] _packet is nullptr", STDA_MAKE_ERROR(STDA_ERROR_TYPE::VERSUS_BASE, 6, 0)); \

// Session pointer
#define INIT_PLAYER_INFO(_method, _msg, __session) auto pgi = getPlayerInfo((__session)); \
	if (pgi == nullptr) \
		throw exception("[Versus::" + std::string((_method)) + "][Error] player[UID=" + std::to_string((__session)->m_pi.uid) + "] " + std::string((_msg)) + ", mas o game nao tem o info dele guardado. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::VERSUS_BASE, 1, 4)); \

using namespace stdA;

Versus::Versus(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie)
	: VersusBase(_players, _ri, _rv, _channel_rookie), m_versus_state(false) {

	// Updates Treasure Hunter System Course
	/*if (!TreasureHunterSystem::isLoad())
		TreasureHunterSystem::load();*/
	if (!sTreasureHunterSystem::getInstance().isLoad())
		sTreasureHunterSystem::getInstance().load();

	/*auto course = TreasureHunterSystem::findCourse((m_ri.course & 0x7F));*/
	auto course = sTreasureHunterSystem::getInstance().findCourse((m_ri.course & 0x7F));

	if (course == nullptr)
		_smp::message_pool::getInstance().push(new message("[Versus::Versus][Error] tentou pegar o course do Treasure Hunter System, mas o course[COURSE="
				+ std::to_string((unsigned short)(m_ri.course & 0x7F)) + "] nao existe no sistema", CL_FILE_LOG_AND_CONSOLE));
	else
		//TreasureHunterSystem::updateCoursePoint(*course, -1);	// -1 point for each game started
		sTreasureHunterSystem::getInstance().updateCoursePoint(*course, -1);	// -1 point for each game started

	// Here the players info must be initialized
	initAllPlayerInfo();

	// Last 5 Players Play, must be saved on the server and in the DB, and Achievement
	for (auto& el : m_players) {

		// Achievement
		INIT_PLAYER_INFO("Versus", "tentou inicializar o counter item do Versus", el);

		initAchievement(*el);

		pgi->sys_achieve.incrementCounter(0x6C40001Du/*Versus*/);
		// End of init Achievement

		for (auto& el2 : m_players) {

			if (el->m_pi.uid != el2->m_pi.uid)
				el->m_pi.l5pg.add(el2->m_pi, el->m_pi.mi.sexo);
		}

		// Update ON DB
		snmdb::NormalManagerDB::getInstance().add(1, new CmdUpdateLastPlayerGame(el->m_pi.uid, el->m_pi.l5pg), Versus::SQLDBResponse, this);
	}

	m_versus_state = init_game();
}

Versus::~Versus() {

	// Stops the player Turn time
	stopTime();

	// Saves the data of all players
	for (auto& el : m_players)
		finish_game(*el);

	deleteAllPlayer();
}

bool Versus::deletePlayer(player* _session, int _option) {

	if (_session == nullptr)
		throw exception("[Versus::deletePlayer][Error] tentou deletar um player, mas o seu endereco eh nullptr.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::VERSUS, 50, 0));
	
	bool ret = false;

	try {

		// Avoid deadlock with the checkVersusTurn thread - Lock
		m_state_vs.lock();

#if defined(_WIN32)
		EnterCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_lock(&m_cs);
#endif

		auto it = std::find(m_players.begin(), m_players.end(), _session);

		if (it != m_players.end()) {
			unsigned char opt = 3;	// Left Quit

			INIT_PLAYER_INFO("deletePlayer", "tentou sair do jogo", _session);

			if (m_game_init_state == 1/*Started*/) {

				packet p;

				// Player Turn Stops his time
				if (m_player_turn == pgi)
					stopTime();

				auto sessions = getSessions(*it);

				requestFinishItemUsedGame(*(*it));	// Saves items used in the Tourney

				requestSaveInfo(*(*it), (_option == 0x800) ? 5/*Does not count quit*/ : 1); // Quit or got DC

				//pgi->flag = PlayerGameInfo::eFLAG_GAME::QUIT;
				setGameFlag(pgi, PlayerGameInfo::eFLAG_GAME::QUIT);

				// Response Player left the Game, removes him from the score list
				p.init_plain((unsigned short)0x61);

				p.addUint32((*it)->m_oid);

				packet_func::vector_send(p, sessions, 1);

				// Response Player left the game MSG
				p.init_plain((unsigned short)0x40);

				p.addUint8(2);	// Player Left Msg

				p.addString((*it)->m_pi.nickname);

				p.addUint16(0);	// size Msg, no need for a msg, pangya already sends it in opt 2

				packet_func::vector_send(p, sessions, 1);

				sendUpdateInfoAndMapStatistics(*_session, -1);

				ret = checkNextStepGame(*_session);

			}else if (m_game_init_state == 2 && !pgi->finish_game) {
	
				// Finished
				requestSaveInfo(*(*it), 0);
			}
			
			// Deletes the player for give up or time out, counts his achievements, has the counter item 0x6C400004u Normal Game Complete
			// Sends the achievements to him to be the same as the original
			if (m_game_init_state == 1/*Started*/ && pgi->data.bad_condute >= 3 && (pgi->data.time_out >= 3 || pgi->data.giveup >= 3)) {

				// Achievements
				rain_hole_consecutivos_count(*_session);			// counts the rain achievements on consecutive holes

				score_consecutivos_count(*_session);				// counts the player's back-to-back achievements (2 or more equal consecutive scores)

				rain_count(*_session);								// Here rain count achievement

				pgi->sys_achieve.incrementCounter(0x6C400004u/*Normal game complete*/);

				// Achievement Here
				pgi->sys_achieve.finish_and_update(*_session);

				// Response that is always sent when a game ends, I do not know what it is yet, only HIO Event does not have it
				packet p((unsigned short)0x244);

				p.addUint32(0);	// OK

				packet_func::session_send(p, _session, 1);

				// This one is new from JP, it has Tourney, VS, Grand Prix, HIO Event, I did not see it, maybe the others have it too
				p.init_plain((unsigned short)0x24F);

				p.addUint32(0);	// OK

				packet_func::session_send(p, _session, 1);
			}

			// Delete Player
			m_players.erase(it);
		}else
			_smp::message_pool::getInstance().push(new message("[Versus::deletePlayer][WARNING] player ja foi excluido do game.", CL_FILE_LOG_AND_CONSOLE));

#if defined(_WIN32)
		LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_unlock(&m_cs);
#endif

		// Avoid deadlock with the checkVersusTurn thread - Unlock
		m_state_vs.unlock();

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[Versus::deletePlayer][Error] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));

		// Releases Critical Section
#if defined(_WIN32)
		LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_unlock(&m_cs);
#endif

		// Avoid deadlock with the checkVersusTurn thread - Unlock
		m_state_vs.unlock();
	}

	return ret;
}

void Versus::deleteAllPlayer() {

	while (!m_players.empty())
		deletePlayer(*m_players.begin(), 0);

}

void Versus::changeHole() {

	updateTreasureHunterPoint();

	if (m_players.size() <= 0 || checkEndGame(**m_players.begin()))
		finish_versus(0);
	else if (m_players.size() > 0)
		// Response finished the hole
		updateFinishHole();	// Finished
}

void Versus::finishHole() {

#if defined(_WIN32)
	EnterCriticalSection(&m_cs);
#elif defined(__linux__)
	pthread_mutex_lock(&m_cs);
#endif

	for (auto& el : m_players) {

		requestFinishHole(*el, 0);

		requestUpdateItemUsedGame(*el);
	}

#if defined(_WIN32)
	LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
	pthread_mutex_unlock(&m_cs);
#endif
}

void Versus::finish_versus(int _option) {

	if (m_players.size() > 0 && m_game_init_state == 1) {

		for (auto& el : m_players) {

			INIT_PLAYER_INFO("finish_versus", "tentou terminar o versus", el);

			pgi->sys_achieve.incrementCounter(0x6C400004u/*Normal game complete*/);

			requestCalculePang(*el);

			updatePlayerAssist(*el);

			sendFinishMessage(*el);
		}

		finish();
	}
}

void Versus::timeIsOver(void* _quem) {

	// Calls timeIsOver of the parent class
	VersusBase::timeIsOver(_quem);

	if (_quem != nullptr) {

		player* p = reinterpret_cast< player* >(_quem);

		INIT_PLAYER_INFO("timeIsOver", "tentou acabar o tempo do turno no jogo", p);

		pgi->tempo = 1u;

		if (pgi->bar_space.getState() == 0 && pgi == m_player_turn) {

			pgi->tempo = 0u;

			if (++pgi->data.time_out >= 3)
				// 3 Time outs kick the player from the room
				pgi->data.bad_condute = 3;	// Kick Player

			// Time Out
			packet p((unsigned short)0x5C);

			p.addUint32(pgi->oid);

			packet_func::game_broadcast(*this, p, 1);
		}

	}else
		_smp::message_pool::getInstance().push(new message("[Versus::timeIsOver][WARNING] time is over executed without _quem, _quem is invalid(nullptr). Bug" , CL_FILE_LOG_AND_CONSOLE));
}

bool Versus::init_game() {

	auto lixo = VersusBase::init_game();

	if (m_players.size() > 0) {

		// variable that saves the system local date
		initGameTime();

		// Here rain count achievement
		// This one has to be at the time the game finishes because it depends on how many holes the player completed
		//rain_count_players();

		m_game_init_state = 1;	// Started

		m_versus_state = true;
	}

	return true;
}

void Versus::requestFinishExpGame() {

	if (m_players.size() > 0) {

		player *_session = nullptr;
		float stars = m_course->getStar();
		int32_t exp = 0, hole_seq = 0;

		for (auto i = 0u; i < m_player_order.size(); ++i) {

			hole_seq = (int)m_course->findHoleSeq(m_player_order[i]->hole);

			// He is on the first hole and did not hit it, only gives experience if he hit the hole
			if (hole_seq == 1 && !m_player_order[i]->shot_sync.state_shot.display.stDisplay.acerto_hole)
				hole_seq = 0;
			
			if ((_session = findSessionByUID(m_player_order[i]->uid)) != nullptr) {

				exp = (int)(1 * m_player_order.size() * (hole_seq > 0 ? hole_seq : 0) * stars);
				exp = (int)(exp * TRANSF_SERVER_RATE_VALUE(m_player_order[i]->used_item.rate.exp) * TRANSF_SERVER_RATE_VALUE(m_rv.exp));
				exp = (int)((float)exp * (float)(1.f - (i * 0.1f)));

				if (m_player_order[i]->level < 70/*Last level does not get exp*/)
					m_player_order[i]->data.exp = exp;
			}

			_smp::message_pool::getInstance().push(new message("[Versus::requestFinishExpGame][Log] player[UID=" + std::to_string(m_player_order[i]->uid) + "] ganhou " + std::to_string(m_player_order[i]->data.exp) + " de experience.", CL_FILE_LOG_AND_CONSOLE));

		}
	}
}

void Versus::finish() {

	m_versus_state = false;	// Versus finished

	m_game_init_state = 2; // Game finished

	requestCalculeRankPlace();

	requestFinishExpGame();

	requestDrawTreasureHunterItem();

	for (auto& el : m_players) {

		INIT_PLAYER_INFO("finish", "tentou finalizar os dados do jogador no jogo", el);

		if (pgi->flag != PlayerGameInfo::eFLAG_GAME::QUIT)
			requestFinishData(*el);
	}

}

void Versus::requestFinishData(player& _session) {

	// Finish Artefact Frozen Flame is now directly in Finish Item Used Game
	requestFinishItemUsedGame(_session);

	requestSaveDrop(_session);

	rain_hole_consecutivos_count(_session);			// counts the rain achievements on consecutive holes

	score_consecutivos_count(_session);				// counts the player's back-to-back achievements (2 or more equal consecutive scores)

	rain_count(_session);							// Here rain count achievement

	//INIT_PLAYER_INFO("requestFinishData", "tentou finalizar dados do jogo", &_session);

	// Response Treasure Hunter Item Draw
	sendTreasureHunterItemDrawGUI(_session);

	// Response finished game - Drop Items
	sendDropItem(_session);

	// Response finished game - Scoreboard
	sendPlacar(_session);
}

bool Versus::finish_game(player& _session, int option) {
	
	if (
#if defined(_WIN32)
		_session.m_sock != INVALID_SOCKET 
#elif defined(__linux__)
		_session.m_sock.fd != INVALID_SOCKET 
#endif
	&& _session.getState() && _session.isConnected() && m_players.size() > 0) {

		INIT_PLAYER_INFO("finish_game", "tentou finalizar o jogo", &_session);

		// Finished the hole, finish the hole for him
		if (pgi->shot_sync.state_shot.display.stDisplay.acerto_hole || pgi->data.giveup) {
			
			requestFinishHole(_session, 0);

			requestUpdateItemUsedGame(_session);
		}

		pgi->finish_game = 1u;

		if (PlayersCompleteGameAndClear() || option == 2/*Ends the game*/) {

			packet p;

			// Checks if it is the first hole and if not everyone finished the hole
			if (m_course->findHoleSeq(pgi->hole) == 1 && !checkAllClearHole() && (pgi->progress.hole <= 0 || pgi->progress.finish_hole[pgi->progress.hole - 1] == 0/*Did not finish the hole*/)) {

				for (auto& el : m_players) {

					INIT_PLAYER_INFO("finish_game", "tentou finalizar o versus", el);

					if (pgi->flag == PlayerGameInfo::eFLAG_GAME::PLAYING) {

						requestSaveInfo(*el, 2);

						if (pgi->finish_item_used == 0u)
							requestFinishItemUsedGame(*el);

						p.init_plain((unsigned short)0x67);

						packet_func::session_send(p, el, 1);

						//pgi->flag = PlayerGameInfo::eFLAG_GAME::END_GAME;
						setGameFlag(pgi, PlayerGameInfo::eFLAG_GAME::END_GAME);
					}
				}

				m_game_init_state = 2;	// VS ended

				return true;

			}else {

				if (m_versus_state) // Lets the client send the packet to finish the game, after it shows the scoreboards
					finish_versus(1);
				else {

					for (auto& el : m_players) {

						INIT_PLAYER_INFO("finish_game", "tentou finalizar o versus", el);

						if (pgi->flag == PlayerGameInfo::eFLAG_GAME::PLAYING) {

							requestSaveRecordCourse(*el, 0/*Normal Game*/, (m_ri.qntd_hole == 18 && m_course->findHoleSeq(pgi->hole) == 18) ? 1 : 0);

							requestSaveInfo(*el, 0);

							// Gives Exp to the Caddie AND Mascot Too
							if (pgi->data.exp > 0) {	// only add exp if greater than 0

								// Add Exp to the player
								el->addExp(pgi->data.exp, false/*No need for the packet to change level*/);

								// Gives Exp to the Equipped Caddie
								if (el->m_pi.ei.cad_info != nullptr)	// Has an equipped caddie
									el->addCaddieExp(pgi->data.exp);

								// Gives Exp to the Equipped Mascot
								if (el->m_pi.ei.mascot_info != nullptr)
									el->addMascotExp(pgi->data.exp);
							}

							sendUpdateInfoAndMapStatistics(*el, 0);

							// Response Treasure Hunter Item
							requestSendTreasureHunterItem(*el);

							// Update Mascot Info ON GAME, if the player has a mascot equipped
							if (el->m_pi.ei.mascot_info != nullptr) {
								packet_func::pacote06B(p, el, &el->m_pi, 8);

								packet_func::session_send(p, el, 1);
							}

							// Achievement Here
							pgi->sys_achieve.finish_and_update(*el);

							// Response that is always sent when a game ends, I do not know what it is yet, only HIO Event does not have it
							p.init_plain((unsigned short)0x244);

							p.addUint32(0);	// OK

							packet_func::session_send(p, &_session, 1);

							// This one is new from JP, it has Tourney, VS, Grand Prix, HIO Event, I did not see it, maybe the others have it too
							p.init_plain((unsigned short)0x24F);

							p.addUint32(0);	// OK

							packet_func::session_send(p, &_session, 1);

							// Response Update Pang
							p.init_plain((unsigned short)0xC8);

							p.addUint64(el->m_pi.ui.pang);

							p.addUint64(0ull);

							packet_func::session_send(p, el, 1);

							//pgi->flag = PlayerGameInfo::eFLAG_GAME::FINISH;
							setGameFlag(pgi, PlayerGameInfo::eFLAG_GAME::FINISH);
						}
					}

					m_game_init_state = 2;	// VS ended

					return true;
				}
			}
		}
	}

	return m_players.size() == 0;
}

void Versus::SQLDBResponse(uint32_t _msg_id, pangya_db& _pangya_db, void* _arg) {

	if (_arg == nullptr) {
		_smp::message_pool::getInstance().push(new message("[Versus::SQLDBResponse][WARNING] _arg is nullptr com msg_id = " + std::to_string(_msg_id), CL_FILE_LOG_AND_CONSOLE));
		return;
	}

	// For now it just leaves, I will add another kind of handling later if needed
	if (_pangya_db.getException().getCodeError() != 0) {
		_smp::message_pool::getInstance().push(new message("[Versus::SQLDBResponse][Error] " + _pangya_db.getException().getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
		return;
	}

	auto *game = reinterpret_cast<Game*>(_arg);

	switch (_msg_id) {
	case 1:	// Update Last 5 Player Game
	{
		auto cmd_l5pg = reinterpret_cast< CmdUpdateLastPlayerGame* >(&_pangya_db);

		_smp::message_pool::getInstance().push(new message("[Versus::SQLDBResponse][Log] player[UID=" + std::to_string(cmd_l5pg->getUID()) + "] atualizou o Last 5 Player Game dele com sucesso!", CL_FILE_LOG_AND_CONSOLE));
		break;
	}
	case 0:
	default:
		break;
	}
}
