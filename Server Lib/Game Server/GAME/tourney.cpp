// File tourney.cpp
// Created on 22/09/2018 at 12:15 by Acrisio
// Implementation of the Tourney class

#if defined(_WIN32)
#pragma pack(1)
#endif

#if defined(_WIN32)
#include <WinSock2.h>
#endif

#include "tourney.hpp"
#include "../../Projeto IOCP/UTIL/exception.h"
#include "../../Projeto IOCP/TYPE/stda_error.h"
#include "../../Projeto IOCP/UTIL/message_pool.h"

#include "treasure_hunter_system.hpp"

#include "../PACKET/packet_func_sv.h"

#include "../PANGYA_DB/cmd_insert_ticket_report.hpp"
#include "../PANGYA_DB/cmd_insert_ticket_report_data.hpp"

#include "../Game Server/game_server.h"

#include "../UTIL/lottery.hpp"
#include "../../Projeto IOCP/UTIL/util_time.h"

#include "../GAME/item_manager.h"

#include "../UTIL/map.hpp"

#define CHECK_SESSION_BEGIN(method) if (!_session.getState()) \
										throw exception("[Tourney::" + std::string((method)) +"][Error] player nao esta connectado.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 1, 0)); \

#define REQUEST_BEGIN(method) CHECK_SESSION_BEGIN(std::string("request") + (method)) \
							  if (_packet == nullptr) \
									throw exception("[Tourney::request" + std::string((method)) +"][Error] _packet is nullptr", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 6, 0)); \

// Session pointer
#define INIT_PLAYER_INFO(_method, _msg, __session) auto pgi = getPlayerInfo((__session)); \
	if (pgi == nullptr) \
		throw exception("[Tourney::" + std::string((_method)) + "][Error] player[UID=" + std::to_string((__session)->m_pi.uid) + "] " + std::string((_msg)) + ", mas o game nao tem o info dele guardado. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 1, 4)); \

using namespace stdA;

Tourney::Tourney(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie)
	: TourneyBase(_players, _ri, _rv, _channel_rookie), m_tourney_state(false), m_pTimer_after_enter(nullptr) {

	// Updates Treasure Hunter System Course
	/*if (!TreasureHunterSystem::isLoad())
		TreasureHunterSystem::load();*/
	if (!sTreasureHunterSystem::getInstance().isLoad())
		sTreasureHunterSystem::getInstance().load();

	/*auto course = TreasureHunterSystem::findCourse(m_ri.course & 0x7F);*/
	auto course = sTreasureHunterSystem::getInstance().findCourse(m_ri.course & 0x7F);

	if (course == nullptr)
		_smp::message_pool::getInstance().push(new message("[Tourney::Tourney][Error] tentou pegar o course do Treasure Hunter System, mas o course[COURSE="
				+ std::to_string((unsigned short)(m_ri.course & 0x7F)) + "] nao existe no sistema", CL_FILE_LOG_AND_CONSOLE));
	else
		//TreasureHunterSystem::updateCoursePoint(*course, -1);	// -1 point for each game started
		sTreasureHunterSystem::getInstance().updateCoursePoint(*course, -1);	// -1 point for each game started

	// Here the players info must be initialized
	initAllPlayerInfo();

	for (auto& el : m_players) {

		INIT_PLAYER_INFO("Tourney", "tentou inicializar o counter item do Tourney", el);

		initAchievement(*el);

		pgi->sys_achieve.incrementCounter(0x6C40001Fu/*Tourney*/);
	}

	m_state = init_game();
}

Tourney::~Tourney() {

	m_tourney_state = false;

	if (m_game_init_state != 2)
		finish();

	while (!PlayersCompleteGameAndClear())
#if defined(_WIN32)
		Sleep(500);
#elif defined(__linux__)
		usleep(500000);
#endif

	deleteAllPlayer();

	clear_time_after_enter();

#ifdef _DEBUG
	_smp::message_pool::getInstance().push(new message("[Tourney::~Tourney][Log] Tourney destroyed on Room[Number=" + std::to_string(m_ri.numero) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
}

bool Tourney::deletePlayer(player* _session, int _option) {

	if (_session == nullptr)
		throw exception("[Tourney::deletePlayer][Error] tentou deletar um player, mas o seu endereco eh nullptr.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 50, 0));
	
	bool ret = false;

	try {

#if defined(_WIN32)
		EnterCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_lock(&m_cs);
#endif

		auto it = std::find(m_players.begin(), m_players.end(), _session);

		if (it != m_players.end()) {
			unsigned char opt = 3;	// Left Quit

			if (m_game_init_state == 1/*Started*/) {

				packet p;

				INIT_PLAYER_INFO("deletePlayer", "tentou sair do jogo", _session);

				auto sessions = getSessions(*it);

				if (pgi->flag != PlayerGameInfo::eFLAG_GAME::TICKET_REPORT) {

					requestFinishItemUsedGame(*(*it));	// Saves items used in the Tourney

					requestSaveInfo(*(*it), (_option == 0x800) ? 5/*Does not count quit*/ : 1); // Quit or got DC

					//pgi->flag = PlayerGameInfo::eFLAG_GAME::QUIT;
					setGameFlag(pgi, PlayerGameInfo::eFLAG_GAME::QUIT);

					// Response Player left the Game, removes him from the score list
					p.init_plain((unsigned short)0x61);

					p.addUint32((*it)->m_oid);

					packet_func::vector_send(p, sessions, 1);

					// Response Player left the game
					sendUpdateState(*_session, opt);

					// Saves the player's Achievement

					if (AllCompleteGameAndClear())
						ret = true;	// Finishes the Tourney

				}else if (_option == 10) {	// Ticket Reporting
					opt = 1;	// Ticket Reporting

					// Response Player left the Game, removes him from the score list
					p.init_plain((unsigned short)0x61);

					p.addUint32((*it)->m_oid);

					packet_func::vector_send(p, sessions, 1);

					// Response Player left the game with ticket reporting
					p.init_plain((unsigned short)0x11B);

					p.addUint32((*it)->m_oid);

					packet_func::vector_send(p, sessions, 1);
				}

				if (opt != 1)	// !Ticket Report
					sendUpdateInfoAndMapStatistics(*_session, -1);
			}

			// Delete Player
			m_players.erase(it);
		}else
			_smp::message_pool::getInstance().push(new message("[Tourney::deletePlayer][WARNING] player ja foi excluido do game.", CL_FILE_LOG_AND_CONSOLE));

#if defined(_WIN32)
		LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_unlock(&m_cs);
#endif

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[Tourney::deletePlayer][Error] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));

		// Releases Critical Section
#if defined(_WIN32)
		LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_unlock(&m_cs);
#endif
	}

	return ret;
}

void Tourney::deleteAllPlayer() {

	while (!m_players.empty())
		deletePlayer(*m_players.begin(), 0);
}

// Game->Course->Hole methods
bool Tourney::requestFinishLoadHole(player& _session, packet *_packet) {
	REQUEST_BEGIN("FinishLoadHole");

	packet p;

	// This one is to Change Room Info
	// to put the room in the mode where players can enter after it has started
	bool ret = false;

	try {

		// Calls the base function to do its part
		ret = TourneyBase::requestFinishLoadHole(_session, _packet);

		// Here starts the time during which other players can enter if the room is not private
		// Starts the 5 or 10min time to enter the camp if there is no password
		if (m_entra_depois_flag != 1 && m_ri.senha_flag == 1 && (m_ri.course & 0x7F) != RoomInfo::TIPO::SPECIAL_SHUFFLE_COURSE) {

			// Only releases if it is a Normal Tourney, if it is a GM Event it does not release
			if (!(m_ri.trofel == TROFEL_GM_EVENT_TYPEID && m_ri.max_player > 30 && m_ri.flag_gm && m_ri.state_flag == 0x100))
				// Allows entering, even after the Tourney has started
				ret = true;

			m_entra_depois_flag = 1;
		}

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[Tourney::requestFinishLoadHole][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return ret;
}

void Tourney::changeHole(player& _session) {

	updateTreasureHunterPoint(_session);

	if (checkEndGame(_session))
		finish_tourney(_session, 0);
	else 
		// Response finished the hole
		updateFinishHole(_session, 1);	// Finished
}

void Tourney::finishHole(player& _session) {

	requestFinishHole(_session, 0);

	requestUpdateItemUsedGame(_session);

}

void Tourney::finish_tourney(player& _session, int _option) {

	if (m_players.size() > 0 && m_game_init_state == 1) {

		INIT_PLAYER_INFO("finish_tourney", "tentou terminar o tourney no jogo", &_session);

		if (pgi->flag == PlayerGameInfo::eFLAG_GAME::PLAYING) {

			// Calculates the pangs the player won
			requestCalculePang(_session);

			// Update the player's pang if he has assist on, and is above beginner E
			updatePlayerAssist(_session);

			if (m_game_init_state == 1 && _option == 0) {

				// Shows msg that the player finished the game
				sendFinishMessage(_session);

				// Response finished the hole
				updateFinishHole(_session, 1);

				// Response Finished the Game, or Left
				sendUpdateState(_session, 2);

				// Achievement Counter
				pgi->sys_achieve.incrementCounter(0x6C400004u/*Normal game complete*/);

			}else if (m_game_init_state == 1 && _option == 1) {	// Time is up

				requestFinishHole(_session, 1);		// Time is up

				// add the strokes of the holes he did not play, because time is up
				// ------ The Original does not add the strokes of the rest of the holes the player did not play, when time runs out -------
				//pgi->ui.tacada = pgi->data.total_tacada_num;

				// Shows msg that the player finished the game
				sendFinishMessage(_session);

				// Response finished the hole
				updateFinishHole(_session, 0);

				// Response for time up of the Tourney
				sendTimeIsOver(_session);
			}
		}

		//pgi->flag = (_option == 0) ? PlayerGameInfo::eFLAG_GAME::FINISH : PlayerGameInfo::eFLAG_GAME::END_GAME;
		setGameFlag(pgi, (_option == 0) ? PlayerGameInfo::eFLAG_GAME::FINISH : PlayerGameInfo::eFLAG_GAME::END_GAME);
		
		GetLocalTime(&pgi->time_finish);

		if (AllCompleteGameAndClear() && m_game_init_state == 1)
			finish();	// Sends the packets that end the game Ex: 0xCE, 0x79 etc
	}
}

bool Tourney::requestUseTicketReport(player& _session, packet *_packet) {
	REQUEST_BEGIN("UseTicketReport");

	packet p;

	bool ret = false;

	try {

		UserInfoEx ui{ 0 };

		_packet->readBuffer(&ui, sizeof(UserInfo));

		// here the client passes mad_conduta with hole_in, swapped, mad_conduto <-> hole_in

		INIT_PLAYER_INFO("requestUseTicketReport", "tentou sair do jogo com ticket report", &_session);

		pgi->ui = ui;

		// Checks if he finished the whole Tourney
		if (pgi->flag != PlayerGameInfo::eFLAG_GAME::FINISH)
			throw exception("[Tourney::requestUseTicketReport][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou sair do jogo na sala[NUMERO="
					+ std::to_string(m_ri.numero) + "] com Ticket Report, mas ele ainda nao terminou o Tourney[FLAG=" + std::to_string((unsigned short)pgi->flag) + "]", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 403, 0));

		// Checks if the player's Level is greater than or equal to Beginner E
		if (_session.m_pi.level < PlayerInfo::enLEVEL::BEGINNER_E)
			throw exception("[Tourney::requestUseTicketReport][Error] player[UID=" + std::to_string(_session.m_pi.uid) + ", LEVEL=" + std::to_string(_session.m_pi.level) 
					+ "] tentou sair do jogo na sala[NUMERO=" + std::to_string(m_ri.numero) 
					+ "] com Ticket Report, mas ele nao tem o level necessario[6=BEGINNER E] para usar o Ticket Report.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 405, 0));

		// Checks if the player has the ticket report
		auto pWi = _session.m_pi.findWarehouseItemByTypeid(TICKET_REPORT_TYPEID);

		// Does not have the item or does not have the quantity "it is the same thing, I am only doing this to prevent bugs"
		if (pWi == nullptr || pWi->STDA_C_ITEM_QNTD < 1)
			throw exception("[Tourney::requestUseTicketReport][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou sair do jogo na sala[NUMERO=" 
					+ std::to_string(m_ri.numero) + "] com Ticket Report, mas ele nao tem o item[TYPEID=" 
					+ std::to_string(TICKET_REPORT_TYPEID) + "]", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 400, 0));

		// Removes a Ticket Report from him
		stItem item{ 0 };

		item.type = 2;
		item.id = pWi->id;
		item._typeid = pWi->_typeid;
		item.qntd = 1;
		item.STDA_C_ITEM_QNTD = (short)item.qntd * -1;

		if (item_manager::removeItem(item, _session) <= 0)
			throw exception("[Tourney::requestUseTicketReport][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou sair do jogo na sala[NUMERO="
					+ std::to_string(m_ri.numero) + "] com Ticket Report, mas nao conseguiu deletar um Ticket Report Item do player.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 401, 0));

		auto v_item = std::vector< stItem >{ item };

		// Log
		_smp::message_pool::getInstance().push(new message("[Tourney::requestUseTicketReport][Log] Player[UID=" + std::to_string(_session.m_pi.uid) 
				+ "] sai do tourney na sala[NUMERO=" + std::to_string(m_ri.numero) + ", MASTER=" + std::to_string(m_ri.master) + "] com ticket report.", CL_FILE_LOG_AND_CONSOLE));

		// Response to make sure the ticket report was really deleted from the player
		packet_func::pacote0AA(p, &_session, v_item);

		packet_func::session_send(p, &_session, 1);

		// Left with Ticket Report
		setGameFlag(pgi, PlayerGameInfo::eFLAG_GAME::TICKET_REPORT);

		rain_hole_consecutivos_count(_session);			// counts the rain achievements on consecutive holes

		score_consecutivos_count(_session);				// counts the player's back-to-back achievements (2 or more equal consecutive scores)

		rain_count(_session);							// Here rain count achievement

		finish_game(_session, 1);

		ret = true;	// Confirms leaving the room

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[Tourney::requestUseTicketReport][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));

		// Error response here, I have to find a packet for this
	}

	return ret;
}

void Tourney::requestStartAfterEnter(job& _job) {

	// Here starts the time during which other players can enter if the room is not private
	// Starts the 5 or 10min time to enter the camp if there is no password

	DWORD milliseconds = 0u;

	if (m_ri.qntd_hole == 18)
		milliseconds = 10 * 60000; // 10min
	else if (m_ri.qntd_hole == 9)
		milliseconds = 5 * 60000; // 5min

	//if (sgs::gs != nullptr)
		// Create Timer
		m_pTimer_after_enter = sgs::gs::getInstance().makeTime(milliseconds, _job);
	/*else
		_smp::message_pool::getInstance().push(new message("[Tourney::requestStartAfterEnter][Error] tentou inicializar um timer, mas a variavel global estatica do Server eh invalida.", CL_FILE_LOG_AND_CONSOLE));*/

#ifdef _DEBUG
	_smp::message_pool::getInstance().push(new message("[Tourney::requestStartAfterEnter][Log] Criou o Timer[Tempo=" + std::to_string((milliseconds > 0) ? milliseconds / 60000 : 0) + "min, STATE=" + std::to_string(m_timer->getState()) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
}

void Tourney::requestEndAfterEnter() {

	// Limpa timer After Enter
	clear_time_after_enter();

	// Send Response to everyone that the time to enter the room is over
	packet p((unsigned short)0x113);

	p.addUint8(8);
	p.addUint8(0);

	p.addUint8((unsigned char)m_player_info.size());

	packet_func::game_broadcast(*this, p, 1);

#ifdef _DEBUG
	_smp::message_pool::getInstance().push(new message("[Tourney::requestEndAfterEnter][Log] Tempo Acabou (After Enter) no Tourney. na sala[NUMERO=" + std::to_string(m_ri.numero) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
}

void Tourney::timeIsOver() {

	if (m_game_init_state == 1 && m_players.size() > 0) {

		player* _session = nullptr;

		for (auto& el : m_player_info) {

			// Only those who did not finish
			if (el.second->flag == PlayerGameInfo::eFLAG_GAME::PLAYING && (_session = findSessionByUID(el.second->uid)) != nullptr)
				finish_tourney(*_session, 1/*Time Up*/);
			else if (el.second->flag == PlayerGameInfo::eFLAG_GAME::FINISH && (_session = findSessionByUID(el.second->uid)) != nullptr)
				// Response for time up of the Tourney
				sendTimeIsOver(*_session);
		}

#ifdef _DEBUG
		_smp::message_pool::getInstance().push(new message("[Tourney::timeIsOver][Log] Tempo Acabou no Tourney. na sala[NUMERO=" + std::to_string(m_ri.numero) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG

	}
}

bool Tourney::init_game() {

	if (m_players.size() > 0) {

		// Creates the Tourney timer
		startTime();

		// variable that saves the system local date
		initGameTime();

		// Here rain count achievement
		// This one has to be at the time the game finishes because it depends on how many holes the player completed
		//rain_count_players();

		m_game_init_state = 1;	// Started

		m_tourney_state = true;
	}

	return true;
}

void Tourney::clear_time_after_enter() {

	// Ensure that any exception brings down the server

	try {
		
		if (m_pTimer_after_enter != nullptr)
			sgs::gs::getInstance().unMakeTime(m_pTimer_after_enter);
		
	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[Tourney::clear_time_after_enter][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	m_pTimer_after_enter = nullptr;
}

void Tourney::requestFinishExpGame() {

	// Bug Fix, last player of the camp leaves and or gets dc and nobody is left in the room and the camp exp is calculated
	if (/*m_players.size()*/getCountPlayersGame() > 0) {

		player *_session = nullptr;
		float stars = m_course->getStar();
		int32_t exp = 0, hole_seq = 0;

		for (auto i = 0u; i < m_player_order.size(); ++i) {

			hole_seq = (int)m_course->findHoleSeq(m_player_order[i]->hole);

			// He is on the first hole and did not hit it, only gives experience if he hit the hole
			if (hole_seq == 1 && !m_player_order[i]->shot_sync.state_shot.display.stDisplay.acerto_hole)
				hole_seq = 0;

			if (m_player_order[i]->flag == PlayerGameInfo::eFLAG_GAME::FINISH) {

				if ((_session = findSessionByUID(m_player_order[i]->uid)) != nullptr) {

					exp = (int)(1 * m_player_order.size() * (hole_seq > 0 ? hole_seq : 0) * stars);
					exp = (int)(exp * TRANSF_SERVER_RATE_VALUE(m_player_order[i]->used_item.rate.exp) * TRANSF_SERVER_RATE_VALUE(m_rv.exp));
					exp = (int)(exp * (1 - (i / m_player_info.size())));

					if (m_player_order[i]->level < 70/*Last level does not get exp*/)
						m_player_order[i]->data.exp = exp;
				}

			}else if (m_player_order[i]->flag == PlayerGameInfo::eFLAG_GAME::TICKET_REPORT) {

				exp = (int)(1 * m_player_order.size() * (hole_seq > 0 ? hole_seq : 0) * stars);
				exp = (int)(exp * TRANSF_SERVER_RATE_VALUE(m_player_order[i]->used_item.rate.exp) * TRANSF_SERVER_RATE_VALUE(m_rv.exp));
				exp = (int)(exp * (1 - (i / m_player_info.size())));

				m_player_order[i]->data.exp = exp;
			
			}else if (m_player_order[i]->flag == PlayerGameInfo::eFLAG_GAME::END_GAME) {
				
				if ((_session = findSessionByUID(m_player_order[i]->uid)) != nullptr) {

					exp = (int)(1 * m_player_order.size() * (hole_seq > 0 ? hole_seq : 0) * stars);
					exp = (int)(exp * TRANSF_SERVER_RATE_VALUE(m_player_order[i]->used_item.rate.exp) * TRANSF_SERVER_RATE_VALUE(m_rv.exp));
					exp = (int)(exp * (1 - (i / m_player_info.size())));

					if (m_player_order[i]->level < 70/*Last level does not get exp*/)
						m_player_order[i]->data.exp = exp;
				}
			}

			_smp::message_pool::getInstance().push(new message("[Tourney::requestFinishExpGame][Log] player[UID=" + std::to_string(m_player_order[i]->uid) + "] ganhou " + std::to_string(m_player_order[i]->data.exp) + " de experience.", CL_FILE_LOG_AND_CONSOLE));

		}
	}
}

void Tourney::finish() {

	m_game_init_state = 2;	// Finished

	requestCalculeRankPlace();

	requestMakeMedal();

	requestMakeTrofel();

	requestFinishExpGame();

	requestSaveTicketReport();

	requestSendTicketReport();

	requestGiveMedalAndItens();

	// [The original pangya, when the player leaves the tourney with ticket report,
	// even if he is in the top 3 in the short game the short game top 3 rank achievement does not count]

	for (auto& el : m_players) {

		INIT_PLAYER_INFO("finish", "tentou finalizar os dados do jogador no jogo", el);

		if (pgi->flag != PlayerGameInfo::eFLAG_GAME::QUIT)
			requestFinishData(*el);
	}
}

void Tourney::requestMakeMedal() {
	
	uint32_t all_player = getCountPlayersGame();

	// Medals are only awarded from 18 players in the game
	if (all_player >= 18) {

		std::vector< PlayerGameInfo* > v_all_player;

		Lottery lottery((uint64_t)this);
		Lottery lot_active_item((uint64_t)this);

		Lottery::LotteryCtx *ctx_lot = nullptr;
		PlayerGameInfo *pgi = nullptr;

		// Active Item that the player wins
		for (auto i = 0u; i < 15u; ++i)
			lot_active_item.push(200, (iff::ITEM << 26) + i);

		// Fills the vector, and feeds the lottery
		for (auto& el : m_player_info) {
			if (el.second->flag != PlayerGameInfo::eFLAG_GAME::QUIT) {
				
				v_all_player.push_back(el.second);

				lottery.push(200, (size_t)&el.second);
			}
		}

		// 1 Lucky Medal
		auto ctx = lottery.spinRoleta();

		if (ctx == nullptr || ctx->value == 0)
			_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeMedal][Error] nao conseguiu sortear um player para ganha a medalha da sorte", CL_FILE_LOG_AND_CONSOLE));
		else {
			pgi = ((PlayerGameInfo*)ctx->value);

			m_medal[0].oid = pgi->oid;

			if ((ctx_lot = lot_active_item.spinRoleta()) == nullptr)
				_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeMedal][Error] nao conseguiu sortear um active commun item da medalha da sorte", CL_FILE_LOG_AND_CONSOLE));
			else
				m_medal[0].item_typeid = (uint32_t)ctx_lot->value;

			pgi->medal_win.stMedal.lucky = 1;
		}

		// 2 Fastest Medal
		std::sort(v_all_player.begin(), v_all_player.end(), Tourney::speediest_sort);

		pgi = *v_all_player.begin();

		m_medal[1].oid = pgi->oid;

		if ((ctx_lot = lot_active_item.spinRoleta()) == nullptr)
			_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeMedal][Error] nao conseguiu sortear um active commun item da medalha de speediest", CL_FILE_LOG_AND_CONSOLE));
		else
			m_medal[1].item_typeid = (uint32_t)ctx_lot->value;

		pgi->medal_win.stMedal.speediest = 1;

		// 3 Best drive Medal (Shot distance)
		std::sort(v_all_player.begin(), v_all_player.end(), Tourney::best_drive_sort);

		pgi = *v_all_player.begin();

		m_medal[2].oid = pgi->oid;

		if ((ctx_lot = lot_active_item.spinRoleta()) == nullptr)
			_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeMedal][Error] nao conseguiu sortear um active commun item da medalha de best drive", CL_FILE_LOG_AND_CONSOLE));
		else
			m_medal[2].item_typeid = (uint32_t)ctx_lot->value;

		pgi->medal_win.stMedal.best_drive = 1;

		// 4 Best Chip-in Medal
		std::sort(v_all_player.begin(), v_all_player.end(), Tourney::best_chipin_sort);

		pgi = *v_all_player.begin();

		m_medal[3].oid = pgi->oid;

		if ((ctx_lot = lot_active_item.spinRoleta()) == nullptr)
			_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeMedal][Error] nao conseguiu sortear um active commun item da medalha de best chipin", CL_FILE_LOG_AND_CONSOLE));
		else
			m_medal[3].item_typeid = (uint32_t)ctx_lot->value;

		pgi->medal_win.stMedal.best_chipin = 1;

		// 5 Best Long Putting Medal
		std::sort(v_all_player.begin(), v_all_player.end(), Tourney::best_long_puttin_sort);

		pgi = *v_all_player.begin();

		m_medal[4].oid = pgi->oid;

		if ((ctx_lot = lot_active_item.spinRoleta()) == nullptr)
			_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeMedal][Error] nao conseguiu sortear um active commun item da medalha de best long puttin", CL_FILE_LOG_AND_CONSOLE));
		else
			m_medal[4].item_typeid = (uint32_t)ctx_lot->value;

		pgi->medal_win.stMedal.best_long_puttin = 1;

		// 6 Best Recovery Medal (Only given if 18h)
		if (m_ri.qntd_hole == 18) {
			std::sort(v_all_player.begin(), v_all_player.end(), Tourney::best_recovery);

			pgi = *v_all_player.begin();

			m_medal[5].oid = pgi->oid;

			if ((ctx_lot = lot_active_item.spinRoleta()) == nullptr)
				_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeMedal][Error] nao conseguiu sortear um active commun item da medalha de best recovery", CL_FILE_LOG_AND_CONSOLE));
			else
				m_medal[5].item_typeid = (uint32_t)ctx_lot->value;

			pgi->medal_win.stMedal.best_recovery = 1;
		}

	}
}

void Tourney::requestMakeTrofel() {

	uint32_t all_player = getCountPlayersGame();

	uint32_t count_trofel = 0u, i = 0u;

	if (m_player_order.size() <= 0)
		requestCalculeRankPlace();

	if (m_player_order.size() != all_player) {

		_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeTrofel][Error] nao conseguiu gerar os trofeus por que o vector de player rank order nao bate com o dos players no jogo", CL_FILE_LOG_AND_CONSOLE));

		return;
	}

	Lottery lottery((uint64_t)this);
	Lottery::LotteryCtx *ctx = nullptr;

	// Active Common Item
	for (i = 0u; i < 15u; ++i)
		lottery.push(200, (iff::ITEM << 26) + i);

	if (m_ri.qntd_hole == 18 && all_player >= 10) {
		// --- 18 Holes Tourney ----
		// 10-14 = 1 bronze
		// 15-18 = 1 silver and 1 bronze
		// 19-22 = 1 gold, 1 silver and 1 bronze
		// 23-26 = 1 gold, 1 silver and 2 bronze
		// 27-30 = 1 gold, 2 silver and 3 bronze

		if (all_player <= 14u)
			count_trofel = 1u;
		else if (all_player <= 18u)
			count_trofel = 2u;
		else if (all_player <= 22u)
			count_trofel = 3u;
		else if (all_player <= 26u)
			count_trofel = 4u;
		else if (all_player <= 30u)
			count_trofel = 6u;
		
	}else if (m_ri.qntd_hole == 9 && all_player >= 15) {
		// --- 9 Holes Tourney ---
		// 15-18 = 1 bronze
		// 19-26 = 1 silver and 1 bronze
		// 27-30 = 1 gold, 1 silver and 1 bronze

		if (all_player <= 18u)
			count_trofel = 1u;
		else if (all_player <= 26u)
			count_trofel = 2u;
		else if (all_player <= 30u)
			count_trofel = 3u;
	}

	// New common item and trophy
	std::vector< PlayerGameInfo* > trofeus;

	if (!trofeus.empty())
		trofeus.clear();

	for (auto& el : m_player_info)
		if (el.second->flag != PlayerGameInfo::eFLAG_GAME::QUIT
				&& (el.first != nullptr && el.first->m_pi.ui.getQuitRate() < QUITER_ICON_2/*Quiter 1 still gets trophy and common item*/))	// except those who quit and the QUITER_ICON_2
			trofeus.push_back(el.second);

	// sort
	std::sort(trofeus.begin(), trofeus.end(), Game::sort_player_rank);

	// give trophies
	if (!trofeus.empty()) {

		if (trofeus.size() < count_trofel) {

			for (i = 6u; i < (count_trofel + 6u) && i < (trofeus.size() + 6u); ++i) {

				m_medal[i].oid = trofeus[i - 6]->oid;

				if ((ctx = lottery.spinRoleta()) == nullptr)
					_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeTrofel][Error] nao conseguiu sortear um active commun item do trofel", CL_FILE_LOG_AND_CONSOLE));
				else
					m_medal[i].item_typeid = (uint32_t)ctx->value;
			}

			// Only gives the trophies if it is not a GM Event
			if (!(m_ri.trofel == TROFEL_GM_EVENT_TYPEID && m_ri.flag_gm && m_ri.max_player > 30 && m_ri.state_flag == 0x100)) {

				switch (count_trofel) {
				case 1:
					trofeus.front()->trofel = 3;
					break;
				case 2:
					for (i = 0u; i < 2u && i < trofeus.size(); ++i)
						trofeus[i]->trofel = (unsigned char)i + 2;
					break;
				case 3:
					for (i = 0u; i < 3u && i < trofeus.size(); ++i)
						trofeus[i]->trofel = (unsigned char)i + 1;
					break;
				case 4:
					for (i = 0u; i < 4u && i < trofeus.size(); ++i)
						trofeus[i]->trofel = (unsigned char)((i < 3u) ? i + 1 : 3);
					break;
				case 6:
					for (i = 0u; i < 6u && i < trofeus.size(); ++i) {
						if (i == 0u)
							trofeus[i]->trofel = (unsigned char)1;
						else if (i < 3u)
							trofeus[i]->trofel = (unsigned char)2;
						else if (i < 6u)
							trofeus[i]->trofel = (unsigned char)3;
					}
					break;
				}
			}

		}else {

			for (i = 6u; i < (count_trofel + 6u); ++i) {

				m_medal[i].oid = trofeus[i - 6]->oid;

				if ((ctx = lottery.spinRoleta()) == nullptr)
					_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeTrofel][Error] nao conseguiu sortear um active commun item do trofel", CL_FILE_LOG_AND_CONSOLE));
				else
					m_medal[i].item_typeid = (uint32_t)ctx->value;
			}

			// Only gives the trophies if it is not a GM Event
			if (!(m_ri.trofel == TROFEL_GM_EVENT_TYPEID && m_ri.flag_gm && m_ri.max_player > 30 && m_ri.state_flag == 0x100)) {
				
				switch (count_trofel) {
				case 1:
					trofeus.front()->trofel = 3;
					break;
				case 2:
					for (i = 0u; i < 2u; ++i)
						trofeus[i]->trofel = (unsigned char)i + 2;
					break;
				case 3:
					for (i = 0u; i < 3u; ++i)
						trofeus[i]->trofel = (unsigned char)i + 1;
					break;
				case 4:
					for (i = 0u; i < 4u; ++i)
						trofeus[i]->trofel = (unsigned char)((i < 3u) ? i + 1 : 3);
					break;
				case 6:
					for (i = 0u; i < 6u; ++i) {
						if (i == 0u)
							trofeus[i]->trofel = (unsigned char)1;
						else if (i < 3u)
							trofeus[i]->trofel = (unsigned char)2;
						else if (i < 6u)
							trofeus[i]->trofel = (unsigned char)3;
					}
					break;
				}
			}
		}
	}

	/*for (i = 6u; i < (count_trofel + 6u); ++i) {

		m_medal[i].oid = m_player_order[i - 6]->oid;

		if ((ctx = lottery.spinRoleta()) == nullptr)
			_smp::message_pool::getInstance().push(new message("[Tourney::requestMakeTrofel][Error] nao conseguiu sortear um active commun item do trofel", CL_FILE_LOG_AND_CONSOLE));
		else
			m_medal[i].item_typeid = ctx->value;
	}

	// Only gives trophies if not a GM Event
	if (!(m_ri.trofel == TROFEL_GM_EVENT_TYPEID && m_ri.flag_gm && m_ri.max_player > 30 && m_ri.state_flag == 0x100)) {
		switch (count_trofel) {
		case 1:
			m_player_order.front()->trofel = 3;
			break;
		case 2:
			for (i = 0u; i < 2u; ++i)
				m_player_order[i]->trofel = (unsigned char)i + 2;
			break;
		case 3:
			for (i = 0u; i < 3u; ++i)
				m_player_order[i]->trofel = (unsigned char)i + 1;
			break;
		case 4:
			for (i = 0u; i < 4u; ++i)
				m_player_order[i]->trofel = (unsigned char)((i < 3u) ? i + 1 : 3);
			break;
		case 6:
			for (i = 0u; i < 6u; ++i) {
				if (i == 0u)
					m_player_order[i]->trofel = (unsigned char)1;
				else if (i < 3u)
					m_player_order[i]->trofel = (unsigned char)2;
				else if (i < 6u)
					m_player_order[i]->trofel = (unsigned char)3;
			}
			break;
		}
	}*/
}

void Tourney::requestSaveTicketReport() {

	// Adds the Tourney Ticket Report
	CmdInsertTicketReport cmd_itr(m_ri.trofel, RoomInfo::TIPO::TOURNEY, true);	// Waiter

	snmdb::NormalManagerDB::getInstance().add(0, &cmd_itr, nullptr, nullptr);

	cmd_itr.waitEvent();

	if (cmd_itr.getException().getCodeError() != 0)
		throw cmd_itr.getException();

	m_tri.clear();
	TicketReportInfo::stTicketReportDados trd{ 0 };

	m_tri.id = cmd_itr.getId();

	for (auto& el : m_player_info) {
		trd.clear();

		trd.uid = el.second->uid;
		trd.exp = el.second->data.exp;
		trd.pang = el.second->data.pang;
		trd.bonus_pang = el.second->data.bonus_pang;
		trd.mascot_typeid = el.second->mascot_typeid;
		trd.flag_item_pang = el.second->boost_item_flag.ucFlag;
		trd.medal = el.second->medal_win;
		trd.premium = el.second->premium_flag;
		trd.score = el.second->data.score;
		trd.state = (el.second->flag == PlayerGameInfo::eFLAG_GAME::QUIT ? 4/*QUIT*/ : 0) | el.second->enter_after_started;
		trd.trofel = el.second->trofel;	// Rank, Gold, Silver and Bronze
		trd.finish_time = el.second->time_finish;

		snmdb::NormalManagerDB::getInstance().add(1, new CmdInsertTicketReportData(m_tri.id, trd), Tourney::SQLDBResponse, this);

		m_tri.v_dados.push_back(trd);
	}
}

void Tourney::requestSendTicketReport() {

	if (m_tri.id != -1) {	// Tourney has Ticket Report

		stItem item{ 0 };
		player* _session = nullptr;
		packet p;

		item.type = 2;
		item._typeid = TICKET_REPORT_SCROLL_TYPEID;
		item.qntd = 1;
		item.STDA_C_ITEM_QNTD = (short)item.qntd;
		
		// Ticket Report ID
		item.c[1] = (short)(m_tri.id / 0x8000);
		item.c[2] = (short)(m_tri.id % 0x8000);
		
		item.flag = 0x20;
		item.flag_time = 0x20;		// Hours
		item.STDA_C_ITEM_TIME = 24;	// 24 Hours

		for (auto& el : m_player_info) {

			// Only for those who left with Ticket Report
			if (el.second->flag == PlayerGameInfo::eFLAG_GAME::TICKET_REPORT) {
				
				item.id = -1;

				// Sends to the online players
				//if (sgs::gs != nullptr) {
					if ((_session = sgs::gs::getInstance().findPlayer(el.second->uid)) != nullptr) {

						// Add Ticket Report Item
						auto rt = item_manager::RetAddItem::T_INIT_VALUE;

						if ((rt = item_manager::addItem(item, *_session, 0, 0)) < 0)
							throw exception("[Tourney::requestSendTicketReport][Error] player[UID=" + std::to_string(el.second->uid) + "], nao conseguiu adicionar o Ticket Report Item[TYPEID="
									+ std::to_string(item._typeid) + "] para o player.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 1, 0));

						// Response for the Ticket Report Treasure Hunter Item(s)
						p.init_plain((unsigned short)0x11C);

						p.addUint8(1);	// OK

						packet_func::session_send(p, _session, 1);

						// Update UserInfo, TrofelInfo and MapStatistics
						sendUpdateInfoAndMapStatistics(*_session, 0);

						if (rt != item_manager::RetAddItem::T_SUCCESS_PANG_AND_EXP_AND_CP_POUCH) {
							
							auto v_item = std::vector< stItem >{ item };

							// Add Ticket Report Item
							packet_func::pacote0AA(p, _session, v_item);

							packet_func::session_send(p, _session, 1);
						}

					}else {	// Player is OFFLINE

						auto rt = item_manager::RetAddItem::T_INIT_VALUE;

						if ((rt = item_manager::addItem(item, el.second->uid, 0, 0)) < 0)
							_smp::message_pool::getInstance().push(new message("[Tourney::requestSendTicketReport][Error] player[UID=" + std::to_string(el.second->uid) + "] nao conseguiu adicionar o Ticket Report item[TYPEID="
									+ std::to_string(item._typeid) + "] para o player", CL_FILE_LOG_AND_CONSOLE));

					}
				//}
			}
		}
	}
}

void Tourney::requestGiveMedalAndItens() {

	std::map< PlayerGameInfo*, std::vector< stItem > > map_item;
	std::map< PlayerGameInfo*, std::vector< stItem > >::iterator map_it;

	std::vector< PlayerGameInfo* > all_player;
	std::vector< PlayerGameInfo* >::iterator it;

	std::vector< stItem > v_item;
	stItem item{ 0 };

	// Fills the vector, and feeds the lottery
	for (auto& el : m_player_info) {
		if (el.second->flag != PlayerGameInfo::eFLAG_GAME::QUIT)
			all_player.push_back(el.second);
	}

	for (auto i = 0u; i < (sizeof(m_medal) / sizeof(m_medal[0])); ++i) {

		// Someone won something
		if (m_medal[i].oid != -1) {

			v_item.clear();
			item.clear();

			// Item
			item.type = 2;
			item.id = -1;
			item._typeid = m_medal[i].item_typeid;
			item.qntd = 1;
			item.STDA_C_ITEM_QNTD = (short)item.qntd;

			v_item.push_back(item);

			// Medals
			if (i < 6) {	// Medals
				item.type = 2;
				item.id = -1;
				item._typeid = (i == 0 ? 0x1A0000F5u : 0x1A0000F0u + (i - 1));
				item.qntd = 1;
				item.STDA_C_ITEM_QNTD = (short)item.qntd;

				v_item.push_back(item);
			}

			if (v_item.empty())
				continue;

			if ((it = std::find_if(all_player.begin(), all_player.end(), [&](auto& _el) {
				return _el->oid == m_medal[i].oid;
			})) == all_player.end()) {
				_smp::message_pool::getInstance().push(new message("[Tourney::requestGiveMedalAndItens][Error] player_info[OID=" + std::to_string(m_medal[i].oid) + "] nao tem nos player_all que ficaram no camp ou saiu com ticket report.", CL_FILE_LOG_AND_CONSOLE));
				continue;
			}

			if ((map_it = map_item.find(*it)) != map_item.end())	// The player is already in the Map, add only the items
				map_it->second.insert(map_it->second.end(), v_item.begin(), v_item.end());
			else
				map_item[*it] = v_item;
		}
	}

	/// Send Items and Trophy
	player *_session = nullptr;

	packet p;

	for (auto& el : map_item) {
		// Items
		if (!el.second.empty()) {

			// Player Online
			if ((_session = sgs::gs::getInstance().findPlayer(el.first->uid)) != nullptr) {
				
				auto rai = item_manager::addItem(el.second, *_session, 0, 0);
				
				if (rai.fails.size() > 0 && rai.type != item_manager::RetAddItem::T_SUCCESS_PANG_AND_EXP_AND_CP_POUCH)
					_smp::message_pool::getInstance().push(new message("[Tourney::requestGiveMedalAndItens][Error] player[UID=" + std::to_string(el.first->uid) + "] nao conseguiu adicionar os itens que ele ganhou com medalhas e trofeus.", CL_FILE_LOG_AND_CONSOLE));

				// Response Add Item to the player
				if (el.second.size() > 0) {

					packet_func::pacote0AA(p, _session, el.second);

					packet_func::session_send(p, _session, 1);
				}

			}else { // Player Offline
				auto rai = item_manager::addItem(el.second, el.first->uid, 0, 0);
				
				if (rai.fails.size() > 0 && rai.type != item_manager::RetAddItem::T_SUCCESS_PANG_AND_EXP_AND_CP_POUCH)
					_smp::message_pool::getInstance().push(new message("[Tourney::requestGiveMedalAndItens][Error] player[UID=" + std::to_string(el.first->uid) + "] nao conseguiu adicionar os itens que ele ganhou com medalhas e trofeus.", CL_FILE_LOG_AND_CONSOLE));
			}
		}

		// Trophies
		if (m_ri.trofel != 0 && el.first->trofel != 0) {
			
			// Player Online
			if ((_session = sgs::gs::getInstance().findPlayer(el.first->uid)) != nullptr) {
				
				_session->m_pi.updateTrofelInfo(m_ri.trofel, el.first->trofel);

				// Update the player's Trophy in the game
				sendUpdateInfoAndMapStatistics(*_session, 0);

			}else	// Player Offline
				PlayerInfo::updateTrofelInfo(el.first->uid, m_ri.trofel, el.first->trofel);
		}

		// Medals - Won medals
		if (el.first->medal_win.ucMedal != 0u) {

			// Player Online
			if ((_session = sgs::gs::getInstance().findPlayer(el.first->uid)) != nullptr) {

				// Update the player's Medal
				_session->m_pi.updateMedal(el.first->medal_win);

				// Update the player's Medal in the game
				sendUpdateInfoAndMapStatistics(*_session, 0);
			
			}else { // Player Offline

				// Update the player's Medal
				PlayerInfo::updateMedal(el.first->uid, el.first->medal_win);
				
			}
		}
	}
}

void Tourney::requestFinishData(player& _session) {

	// Finish Artefact Frozen Flame is now directly in Finish Item Used Game
	requestFinishItemUsedGame(_session);

	requestSaveDrop(_session);

	// GM Tourney has no treasure Hunter Item
	if (!(m_ri.trofel == TROFEL_GM_EVENT_TYPEID && m_ri.max_player > 30 && m_ri.flag_gm && m_ri.state_flag == 0x100))
		requestDrawTreasureHunterItem(_session);

	rain_hole_consecutivos_count(_session);			// counts the rain achievements on consecutive holes

	score_consecutivos_count(_session);				// counts the player's back-to-back achievements (2 or more equal consecutive scores)

	rain_count(_session);							// Here rain count achievement
	
	achievement_top_3_1st(_session);				// If the Player finished in the Top 3 add +1 to the top 3 counter, and if he finished first add +1 to the first one

	//INIT_PLAYER_INFO("requestFinishData", "tentou finalizar dados do jogo", &_session);

	// Response finished game - Drop Items
	sendDropItem(_session);

	// Response finished game - Scoreboard
	sendPlacar(_session);

	// Response Treasure Hunter Item Draw
	sendTreasureHunterItemDrawGUI(_session);
}

void Tourney::requestCalculeShotSpinningCube(player& _session, ShotSyncData& _ssd) {
	CHECK_SESSION_BEGIN("requestCalculeShotSpinningCube");

	try {

		// Only calculates if it is not short game
		if (!m_ri.natural.stBit.short_game)
			calcule_shot_to_spinning_cube(_session, _ssd);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[Tourney::requestCalculeShotSpinningCube][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void Tourney::requestCalculeShotCoin(player& _session, ShotSyncData& _ssd) {
	CHECK_SESSION_BEGIN("requestCalculeShotCoin");

	try {

		// Only calculates if it is not short game
		if (!m_ri.natural.stBit.short_game)
			calcule_shot_to_coin(_session, _ssd);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[Tourney::requestCalculeShotCoin][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

bool Tourney::speediest_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2) {
	
	if (_pgi1->progress.isGoodScore() && !_pgi2->progress.isGoodScore())
		return true;

	return getTimeDiff(_pgi1->time_finish, _pgi2->time_finish) < 0;
}

bool Tourney::best_drive_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2) {

	if (_pgi1->progress.isGoodScore() && !_pgi2->progress.isGoodScore())
		return true;

	return _pgi1->progress.best_drive > _pgi2->progress.best_drive;
}

bool Tourney::best_chipin_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2) {

	if (_pgi1->progress.isGoodScore() && !_pgi2->progress.isGoodScore())
		return true;

	return _pgi1->progress.best_chipin > _pgi2->progress.best_chipin;
}

bool Tourney::best_long_puttin_sort(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2) {

	if (_pgi1->progress.isGoodScore() && !_pgi2->progress.isGoodScore())
		return true;

	return _pgi1->progress.best_long_puttin > _pgi2->progress.best_long_puttin;
}

bool Tourney::best_recovery(PlayerGameInfo* _pgi1, PlayerGameInfo* _pgi2) {

	if (_pgi1->progress.isGoodScore() && !_pgi2->progress.isGoodScore())
		return true;

	return _pgi1->progress.getBestRecovery() < _pgi2->progress.getBestRecovery();
}

bool Tourney::finish_game(player& _session, int option) {
	
	if (
#if defined(_WIN32)
		_session.m_sock != INVALID_SOCKET 
#elif defined(__linux__)
		_session.m_sock.fd != INVALID_SOCKET 
#endif
	&& _session.getState() && _session.isConnected() && m_players.size() > 0) {

		packet p;

		if (option == 6/*packet06 packet that ends the game*/) {

			if (m_tourney_state)
				finish_tourney(_session, 1);	// Ends without having finished playing

			INIT_PLAYER_INFO("finish_game", "tentou terminar o jogo", &_session);

			// Did not finish the Game in time, add the strokes of the other holes he could not finish
			// ------ The Original does not add the strokes of the rest of the holes the player did not play, when time runs out -------
			//if (pgi->flag == PlayerGameInfo::eFLAG_GAME::END_GAME)
				//pgi->ui.tacada = pgi->data.total_tacada_num;

			// Save the record if the camp ended and the player did not finish all the holes, the record must also be saved [OK][Done]
			requestSaveRecordCourse(_session, 0/*Normal Game*/, (m_ri.qntd_hole == 18 && (m_course->findHoleSeq(pgi->hole) == 18 || pgi->flag == PlayerGameInfo::eFLAG_GAME::END_GAME)) ? 1 : 0);

			requestSaveInfo(_session, 0);

			// Gives Exp to the Caddie AND Mascot Too
			if (pgi->data.exp > 0) {	// only add exp if greater than 0

				// Add Exp to the player
				_session.addExp(pgi->data.exp, false/*No need for the packet to change level*/);

				// Gives Exp to the Equipped Caddie
				if (_session.m_pi.ei.cad_info != nullptr)	// Has an equipped caddie
					_session.addCaddieExp(pgi->data.exp);

				// Gives Exp to the Equipped Mascot
				if (_session.m_pi.ei.mascot_info != nullptr)
					_session.addMascotExp(pgi->data.exp);
			}

			// Update Info Map Statistics
			sendUpdateInfoAndMapStatistics(_session, 0);

			// Response Treasure Hunter Item
			requestSendTreasureHunterItem(_session);

			// Update Mascot Info ON GAME, if the player has a mascot equipped
			if (_session.m_pi.ei.mascot_info != nullptr) {
				packet_func::pacote06B(p, &_session, &_session.m_pi, 8);

				packet_func::session_send(p, &_session, 1);
			}

			// Achievement Here
			pgi->sys_achieve.finish_and_update(_session);

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

			p.addUint64(_session.m_pi.ui.pang);

			p.addUint64(0ull);

			packet_func::session_send(p, &_session, 1);

			// Setting finish_game to 1 means he finished the camp
			pgi->finish_game = 1;

			// Flag of the game that finished
			m_game_init_state = 2;	// FINISHED

		}else if (option == 1/*Ticket Report*/) {

			INIT_PLAYER_INFO("finish_game", "tentou terminar o jogo", &_session);

			// Finish Artefact Frozen Flame is now directly in Finish Item Used Game
			requestFinishItemUsedGame(_session);

			requestSaveDrop(_session);

			requestDrawTreasureHunterItem(_session);

			// This is where this one comes, the Save record course and the save info
			requestSaveRecordCourse(_session, 0/*Normal Game*/, (m_ri.qntd_hole == 18 && m_course->findHoleSeq(pgi->hole) == 18) ? 1 : 0);

			requestSaveInfo(_session, 0);

			// Response Treasure Hunter Item Draw
			sendTreasureHunterItemDrawGUI(_session);

			// Response of Leaves with Ticket Report
			p.init_plain((unsigned short)0x12A);

			p.addUint32(0);	// OK

			packet_func::session_send(p, &_session, 1);

			// Update Info Map Statistics
			sendUpdateInfoAndMapStatistics(_session, 0);

			// Response finished game - Drop Items
			sendDropItem(_session);

			// Packet telling to leave the room and return to the normal Lobby because Ticket report can only be used in Tourney
			// Left the room, visual
			packet_func::pacote04C(p, &_session, -1);
			packet_func::session_send(p, &_session, 1);

			// Response Sends the items won in the Treasure Hunter
			requestSendTreasureHunterItem(_session);

			// Update Mascot Info ON GAME, if the player has a mascot equipped
			if (_session.m_pi.ei.mascot_info != nullptr) {
				packet_func::pacote06B(p, &_session, &_session.m_pi, 8);

				packet_func::session_send(p, &_session, 1);
			}

			// Achievement Here
			pgi->sys_achieve.finish_and_update(_session);

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

			p.addUint64(_session.m_pi.ui.pang);

			p.addUint64(0ull);

			packet_func::session_send(p, &_session, 1);

		}
	}

	return (PlayersCompleteGameAndClear() && m_tourney_state);
}
