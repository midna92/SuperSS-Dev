// Arquivo grand_prix.cpp
// Created on 18/06/2019 at 08:33 by Acrisio
// Implementation of the GrandPrix class

#if defined(_WIN32)
#pragma pack(1)
#endif

#if defined(_WIN32)
#include <WinSock2.h>
#endif

#include "grand_prix.hpp"

#include "../PACKET/packet_func_sv.h"

#include "../UTIL/lottery.hpp"
#include "../UTIL/map.hpp"

#include "item_manager.h"
#include "treasure_hunter_system.hpp"

#include "../Game Server/game_server.h"

#include "../../Projeto IOCP/UTIL/random_gen.hpp"

using namespace stdA;

#define CHECK_SESSION_BEGIN(method) if (!_session.getState()) \
										throw exception("[GrandPrix::" + std::string((method)) +"][Error] player nao esta connectado.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 1, 0)); \

#define REQUEST_BEGIN(method) CHECK_SESSION_BEGIN(std::string("request") + (method)) \
							  if (_packet == nullptr) \
									throw exception("[GrandPrix::request" + std::string((method)) +"][Error] _packet is nullptr", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 6, 0)); \

// Session pointer
#define INIT_PLAYER_INFO(_method, _msg, __session) auto pgi = getPlayerInfo((__session)); \
	if (pgi == nullptr) \
		throw exception("[GrandPrix::" + std::string((_method)) + "][Error] player[UID=" + std::to_string((__session)->m_pi.uid) + "] " + std::string((_msg)) + ", mas o game nao tem o info dele guardado. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 1, 4)); \

// Only lets the function be accessed 1x per shot
#define ONCE_PER_SHOT(_method, _msg, _flag, _ret) INIT_PLAYER_INFO((_method), (_msg), &_session); \
\
	m_lock_manager.lock(&_session); \
\
	if (pgi->_flag == 1u) { \
\
		_smp::message_pool::getInstance().push(new message("[GrandPrix::" + std::string((_method)) + "][Error] Player[UID=" + std::to_string(_session.m_pi.uid) \
				+ "] ja enviou esse pacote, ignora ele.", CL_FILE_LOG_AND_CONSOLE)); \
\
		m_lock_manager.unlock(&_session); \
\
		_ret; \
	}else \
		pgi->_flag = 1u; \
\
	m_lock_manager.unlock(&_session); \

GrandPrix::GrandPrix(std::vector< player* >& _players, RoomInfoEx& _ri, RateValue _rv, unsigned char _channel_rookie, IFF::GrandPrixData& _gp) 
	: TourneyBase(_players, _ri, _rv, _channel_rookie), m_gp(_gp), m_gp_reward(), m_bot(), m_grand_prix_state(false), 
		m_timer_manager(), m_timer_manager_rule(), m_lock_manager() {

#if defined(_WIN32)
	InitializeCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	INIT_PTHREAD_MUTEXATTR_RECURSIVE;
	INIT_PTHREAD_MUTEX_RECURSIVE(&m_cs_sync_shot);
	DESTROY_PTHREAD_MUTEXATTR_RECURSIVE;
#endif

	// Treasure Hunter System. diminui o Course Jogado
	if (!sTreasureHunterSystem::getInstance().isLoad())
		sTreasureHunterSystem::getInstance().load();

	auto course = sTreasureHunterSystem::getInstance().findCourse(m_ri.course & 0x7F);

	if (course == nullptr)
		_smp::message_pool::getInstance().push(new message("[GrandPrix::GrandPrix][Error] tentou pegar o course do Treasure Hunter System, mas o course[COURSE="
				+ std::to_string((unsigned short)(m_ri.course & 0x7F)) + "] nao existe no sistema", CL_FILE_LOG_AND_CONSOLE));
	else
		sTreasureHunterSystem::getInstance().updateCoursePoint(*course, -1);	// -1 ponto a cada jogo iniciado

	// Here the players info must be initialized
	initAllPlayerInfo();

	// Load Grand Prix Rank Reward from iff
	m_gp_reward = sIff::getInstance().findGrandPrixRankReward(m_gp.typeid_link);

	// Sort the GrandPrixRankReward from the lowest rank to the highest
	std::sort(m_gp_reward.begin(), m_gp_reward.end(), [](auto& _1, auto& _2) {
		return _1.rank < _2.rank;
	});

	// Init Bots
	init_bots();

	// Class Grand Prix Counter Item Typeid
	uint32_t class_gp_counter_typeid = 0u;

	if (sIff::getInstance().isGrandPrixEvent(m_gp._typeid)) {

		class_gp_counter_typeid = 0x6C4000AEu;

	}else {
			
		switch (sIff::getInstance().getGrandPrixAba(m_gp._typeid)) {
		case IFF::GrandPrixData::GP_ABA::ROOKIE:
			class_gp_counter_typeid = 0x6C4000AAu;
			break;
		case IFF::GrandPrixData::GP_ABA::BEGINNER:
			class_gp_counter_typeid = 0x6C4000ABu;
			break;
		case IFF::GrandPrixData::GP_ABA::JUNIOR:
			class_gp_counter_typeid = 0x6C4000ACu;
			break;
		case IFF::GrandPrixData::GP_ABA::SENIOR:
			class_gp_counter_typeid = 0x6C4000ADu;
			break;
		}

	}

	// Initialize achievement of players
	for (auto& el : m_players) {

		INIT_PLAYER_INFO("GrandPrix", "tentou inicializar o counter item do Grand Prix", el);

		initAchievement(*el);

		pgi->sys_achieve.incrementCounter(0x6C4000A9u/*Grand Prix*/);

		if (class_gp_counter_typeid > 0)
			pgi->sys_achieve.incrementCounter(class_gp_counter_typeid);
	}

	// Consume the Tickets of the players who will play the Grand Prix
	consomeTicket();

	// inicializa o jogo
	m_state = init_game();
}

GrandPrix::~GrandPrix() {

	m_grand_prix_state = false;

	if (m_game_init_state != 2)
		finish();

	while (!PlayersCompleteGameAndClear())
#if defined(_WIN32)
		Sleep(500);
#elif defined(__linux__)
		usleep(500000);
#endif

	deleteAllPlayer();

	if (!m_bot.empty()) {
		m_bot.clear();
		m_bot.shrink_to_fit();
	}

	// Clear timers
	clear_timers();

#if defined(_WIN32)
	DeleteCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_destroy(&m_cs_sync_shot);
#endif

#ifdef _DEBUG
	_smp::message_pool::getInstance().push(new message("[GrandPrix::~GrandPrix][Log] Grand Prix destroyed on Room[Number=" + std::to_string(m_ri.numero) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
}

void GrandPrix::sendInitialData(player& _session) {

	packet p;

	try {

#if defined(_WIN32)
		if (InterlockedIncrement(&m_sync_send_init_data) == m_players.size()) {
#elif defined(__linux__)
		if (__atomic_add_fetch(&m_sync_send_init_data, 1u, __ATOMIC_RELAXED) == m_players.size()) {
#endif

			// Zera Variavel Volatile(atomic)
#if defined(_WIN32)
			InterlockedExchange(&m_sync_send_init_data, 0u);
#elif defined(__linux__)
			__atomic_store_n(&m_sync_send_init_data, 0u, __ATOMIC_RELAXED);
#endif

			// Game Data Init
			p.init_plain((unsigned short)0x76);

			p.addUint8(m_ri.tipo_show);
			p.addUint32(1);

			p.addBuffer(&m_start_time, sizeof(m_start_time));

			packet_func::game_broadcast(*this, p, 1);

			// Here are the GP bots
			p.init_plain((unsigned short)0x256);

			p.addUint32(0u);	// OK [Option Error]

			p.addUint16((unsigned short)m_bot.size());

			for (auto& el : m_bot) {

				p.addUint32(el.id);
				p.addUint8((unsigned char)el.hole.size());

				for (auto& el2 : el.hole)
					p.addBuffer(&el2, sizeof(Bot::Hole));
			}

			packet_func::game_broadcast(*this, p, 1);

			// Course
			// Send Individual Packet to all players in game
			for (auto& el : m_players)
				Game::sendInitialData(*el);
		}

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::sendInitialData][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

}

bool GrandPrix::deletePlayer(player* _session, int _option) {
	
	if (_session == nullptr)
		throw exception("[GrandPrix::deletePlayer][Error] tentou deletar um player, mas o seu endereco eh nullptr.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY, 50, 0));
	
	bool ret = false;

	try {

#if defined(_WIN32)
		EnterCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_lock(&m_cs);
#endif

		auto it = std::find(m_players.begin(), m_players.end(), _session);

		if (it != m_players.end()) {
			unsigned char opt = 3;	// Saiu Quitou

			INIT_PLAYER_INFO("deletePlayer", "tentou sair do jogo", _session);

			// Stop the player's hole time
			stopTime(_session);
			stopTimeRule(_session);

			packet p;

			if (m_game_init_state == 1/*Started*/) {

				auto sessions = getSessions(*it);

				requestFinishItemUsedGame(*(*it));	// Save items used in Tourney

				// Rookie Grand Prix does not change the player info, only achievement
				if (!(sIff::getInstance().getGrandPrixAba(m_gp._typeid) == IFF::GrandPrixData::GP_ABA::ROOKIE && sIff::getInstance().isGrandPrixNormal(m_gp._typeid)))
					requestSaveInfo(*(*it), (_option == 0x800) ? 5/*Does not count quit*/ : 1); // Quit or got DC

				//pgi->flag = PlayerGameInfo::eFLAG_GAME::QUIT;
				setGameFlag(pgi, PlayerGameInfo::eFLAG_GAME::QUIT);

				// Reply Player left the Game, remove him from the score list
				p.init_plain((unsigned short)0x61);

				p.addUint32((*it)->m_oid);

				packet_func::vector_send(p, sessions, 1);

				// Reply Player left the game
				sendUpdateState(*_session, opt);

				if (AllCompleteGameAndClear())
					ret = true;	// Termina o Tourney

				sendUpdateInfoAndMapStatistics(*_session, -1);
			
			}else if (m_game_init_state == 2 && !pgi->finish_game) {

				// Acabou
				
				// Rookie Grand Prix does not change the player info, only achievement
				if (!(sIff::getInstance().getGrandPrixAba(m_gp._typeid) == IFF::GrandPrixData::GP_ABA::ROOKIE && sIff::getInstance().isGrandPrixNormal(m_gp._typeid)))
					requestSaveInfo(*(*it), 0);
			}

			// Delete the player for give up or time out, it counts his achievements, there is the counter item 0x6C400004u Normal Game Complete
			// Send the achievements to him to match the original
			if (m_game_init_state == 1/*Started*/ && pgi->data.bad_condute >= 3 && (pgi->data.time_out > 0 || pgi->data.giveup > 0)) {

				// Achievements
				rain_hole_consecutivos_count(*_session);			// count the rain achievements in consecutive holes

				score_consecutivos_count(*_session);				// count the player's back-to-back achievements (2 or more consecutive equal scores)

				rain_count(*_session);								// Here the rain count achievement

				pgi->sys_achieve.incrementCounter(0x6C400004u/*Normal game complete*/);

				// Achievement Here
				pgi->sys_achieve.finish_and_update(*_session);

				// Reply always sent when a game ends, I do not know what it is yet, only HIO Event does not have this one
				packet p((unsigned short)0x244);

				p.addUint32(0);	// OK

				packet_func::session_send(p, _session, 1);

				// This one is new from JP, has Tourney, VS, Grand Prix, HIO Event, I did not see it, maybe the others have it too
				p.init_plain((unsigned short)0x24F);

				p.addUint32(0);	// OK

				packet_func::session_send(p, _session, 1);
			}

			// Delete Player
			m_players.erase(it);
		}else
			_smp::message_pool::getInstance().push(new message("[GrandPrix::deletePlayer][WARNING] player ja foi excluido do game.", CL_FILE_LOG_AND_CONSOLE));

		// Here, if it is not true, check if everyone finished the hole and send packet255
		if (!ret && checkAllClearHoleAndClear())
			sendAllToNextHole();

#if defined(_WIN32)
		LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_unlock(&m_cs);
#endif

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::deletePlayer][Error] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));

		// Here, if it is not true, check if everyone finished the hole and send packet255
		if (!ret && checkAllClearHoleAndClear())
			sendAllToNextHole();

		// Libera Critical Section
#if defined(_WIN32)
		LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
		pthread_mutex_unlock(&m_cs);
#endif
	}

	return ret;
}

void GrandPrix::deleteAllPlayer() {
	
	while (!m_players.empty())
		deletePlayer(*m_players.begin(), 0);
}

void GrandPrix::requestFinishCharIntro(player& _session, packet *_packet) {
	REQUEST_BEGIN("FinishCharIntro");

	packet p;

	try {

		// Call the base so it does its part
		TourneyBase::requestFinishCharIntro(_session, _packet);

		INIT_PLAYER_INFO("requestFinishCharIntro", "tentou finalizar o character intro do player", &_session);

		m_lock_manager.lock(&_session);

		// Here the player's finish hole2 flag is zeroed
		pgi->finish_hole2 = 0u;
		pgi->finish_hole3 = 0u;

		m_lock_manager.unlock(&_session);

		// Here the player's hole time starts
		if (m_gp.time_hole > 0u)
			startTime(&_session);

	}catch (exception& e) {

		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestFinishCharIntro][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

}

void GrandPrix::requestActiveBooster(player& _session, packet *_packet) {
	REQUEST_BEGIN("ActiveBooster");

	packet p;

	try {

#define TIME_BOOSTER_VELOCIDADE 3.f

		// 3.f Speed 2x Default of the Time Booster, the player is using his own or he is a premium user
		// 2.f < 3.f Speed of the 1.5x Grand Prix Booster that it gives for free. However, Pangya JP does not have this Booster in Grand Prix
		float velocidade = _packet->readFloat();

		INIT_PLAYER_INFO("requestActiveBooster", "tentou ativar Time Booster no jogo", &_session);

		// Booster Normal
		if (velocidade >= TIME_BOOSTER_VELOCIDADE) {

			if (_session.m_pi.m_cap.stBit.premium_user == 0) { // (is not)!PREMIUM USER

				auto pWi = _session.m_pi.findWarehouseItemByTypeid(TIME_BOOSTER_TYPEID);

				if (pWi == nullptr)
					throw exception("[GrandPrix::requestActiveBooster][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou ativar time booster, mas ele nao tem o item passive. Hacker ou Bug", 
							STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 11, 0));

				if (pWi->STDA_C_ITEM_QNTD <= 0)
					throw exception("[GrandPrix::requestActiveBooster][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou ativar time booster, mas ele nao tem quantidade suficiente[VALUE=" 
							+ std::to_string(pWi->STDA_C_ITEM_QNTD) + ", REQUEST=1] do item de time booster.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::TOURNEY_BASE, 12, 0));

				auto it = pgi->used_item.v_passive.find(pWi->_typeid);

				if (it == pgi->used_item.v_passive.end())
					throw exception("[GrandPrix::requestActiveBooster][Error] player[UID = " + std::to_string(_session.m_pi.uid) + "] tentou ativar time booster, mas ele nao tem ele no item passive usados do server. Hacker ou Bug", 
							STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 13, 0));

				if ((short)it->second.count >= pWi->STDA_C_ITEM_QNTD)
					throw exception("[GrandPrix::requestActiveBooster][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou ativar time booster, mas ele ja usou todos os time booster. Hacker ou Bug", 
							STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 14, 0));

				// Add +1 ao item passive usado
				it->second.count++;

			}else { // Add +1 to the player's booster counter item and passive item counter

				pgi->sys_achieve.incrementCounter(0x6C400075u/*Passive Item*/);

				pgi->sys_achieve.incrementCounter(0x6C400050u);
			}
		
		}

		// Reply to Active Booster
		p.init_plain((unsigned short)0xC7);

		p.addFloat(velocidade);
		p.addUint32(_session.m_oid);

		packet_func::session_send(p, &_session, 1);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestActiveBooster][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::requestStartTurnTime(player& _session, packet *_packet) {
	REQUEST_BEGIN("StartTurnTime");

	try {

		INIT_PLAYER_INFO("requestStartTurnTime", "tentou comecar o tempo de rule do player", &_session);

		m_lock_manager.lock(&_session);

		// Limpa a flag init shot
		pgi->init_shot = 0u;

		m_lock_manager.unlock(&_session);

		// Start the Grand Prix Rule time
		if (m_gp.rule > 0)
			startTimeRule(&_session);

	}catch (exception& e) {

		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestStartTurnTime][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::changeHole(player& _session) {

	updateTreasureHunterPoint(_session);

	if (checkEndGame(_session))
		finish_grand_prix(_session, 0);
	else {

		// Resposta terminou o hole
		updateFinishHole(_session, 1);	// Terminou

		// I replaced clear hole and giveup with the finish hole flag. Now it is OK
		if (checkAllClearHole()) {

			clearAllClearHole();

			// Change Hole All Finish Hole
			sendAllToNextHole();
		}
	}
}

void GrandPrix::finishHole(player& _session) {

	try {
		
		ONCE_PER_SHOT("finishHole", "tentou finalizar o hole", finish_hole3, return);

		m_lock_manager.lock(&_session);

		// Stop the player's time
		stopTime(&_session);
		stopTimeRule(&_session);

		// If the player gave up or timed out, do not add the penalties, as he already got the maximum score
		if (pgi->data.time_out == 0u && pgi->data.giveup == 0u)
			// Add the penalties to the player's shots
			pgi->data.tacada_num += pgi->data.penalidade;
		
		// finalize the hole data Game::requestfinishHole
		requestFinishHole(_session, 0);

		// update items used in the game
		requestUpdateItemUsedGame(_session);

		// Limpa flags das tacadas
		pgi->init_shot = 0u;
		pgi->sync_shot_flag = 0u;
		pgi->finish_shot = 0u;

		// Libera
		m_lock_manager.unlock(&_session);

		// Here there is already a synchronization of all players in the game, 
		// if I make the player's locker release after it, it can cause a deadlock
		// Player finished the hole, can now change hole
		setClearHole(pgi);

	}catch (exception& e) {

		// Libera
		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::finishHole][ErrrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

}

void GrandPrix::requestInitShot(player& _session, packet *_packet) {

	try {

		// !@ Teste
		//_smp::message_pool::getInstance().push(new message("[GrandPrix::requestInitShot][Log] Player[UID=" + std::to_string(_session.m_pi.uid) + "] Recebi", CL_FILE_LOG_AND_CONSOLE));

		ONCE_PER_SHOT("requestInitShot", "tentou iniciar tacada no jogo", init_shot, return);
		
		// Stop the rule time of the one who just hit
		stopTimeRule(&_session);

		// Call the parent class function
		TourneyBase::requestInitShot(_session, _packet);

		// Check if the three shots were received and go to the next turn, otherwise change the hole
		//changeTurn(_session);

	}catch (exception& e) {

		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestInitShot][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

/*void GrandPrix::requestSyncShot(player& _session, packet *_packet) {

	try {

		// !@ Teste
		//Sleep(5000);

		// !@ Teste
		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestSyncShot][Log] Player[UID=" + std::to_string(_session.m_pi.uid) + "] Recebi", CL_FILE_LOG_AND_CONSOLE));

		ONCE_PER_SHOT("requestSyncShot", "tentou sincronizar a tacada do player", sync_shot_flag, return);

		TourneyBase::requestSyncShot(_session, _packet);

		// Verifica se as tr�s tacadas foram recebidas e para para o proximo turno outro troca o hole
		changeTurn(_session);

	}catch (exception& e) {

		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestSyncShot][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}*/

/*RetFinishShot GrandPrix::requestFinishShot(player& _session, packet *_packet) {
	
	RetFinishShot ret{ 0 };

	try {

		// !@ Teste
		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestFinishShot][Log] Player[UID=" + std::to_string(_session.m_pi.uid) + "] Recebi", CL_FILE_LOG_AND_CONSOLE));

		ONCE_PER_SHOT("requestFinishShot", "tentou finalizar a tacada do player", finish_shot, return ret);

		LARGE_INTEGER frequency, tick, end_tick;

		QueryPerformanceFrequency(&frequency);

		QueryPerformanceCounter(&tick);

		// Request Init Cube Coin
		auto cube = requestInitCubeCoin(_session, _packet);

		// Resposta para Finish Shot
		sendEndShot(_session, cube);

		ret.ret = changeTurn(_session);

		if (ret.ret == 2)
			ret.p = &_session;

		QueryPerformanceCounter(&end_tick);

#ifdef _DEBUG
		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestFinishShot][Log] player[UID=" + std::to_string(_session.m_pi.uid) + "] finalizar tacada, o Server demorou "
				+ std::to_string((end_tick.QuadPart - tick.QuadPart) * 1000000 / frequency.QuadPart) + " micro-segundos", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG

	}catch (exception& e) {

		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestFinishShot][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
	
	return ret;
}*/

void GrandPrix::finish_grand_prix(player& _session, int _option) {

	if (m_players.size() > 0 && m_game_init_state == 1) {

		INIT_PLAYER_INFO("finish_grand_prix", "tentou terminar o grand prix no jogo", &_session);

		if (pgi->flag == PlayerGameInfo::eFLAG_GAME::PLAYING) {

			// Calculates the pangs the player earned
			requestCalculePang(_session);

			// Rookie Grand Prix only gives 1/3 of the pangs earned
			if (sIff::getInstance().getGrandPrixAba(m_gp._typeid) == IFF::GrandPrixData::GP_ABA::ROOKIE && sIff::getInstance().isGrandPrixNormal(m_gp._typeid)) {
				pgi->data.pang = (uint64_t)(pgi->data.pang * (1.f / 3.f));
				pgi->data.bonus_pang = (uint64_t)(pgi->data.bonus_pang * (1.f / 3.f));
			}

			// Update the player's pang if assist is on and the level is above Beginner E
			updatePlayerAssist(_session);

			if (m_game_init_state == 1 && _option == 0) {

				// Shows msg that the player finished the game
				sendFinishMessage(_session);

				// Response: hole finished
				updateFinishHole(_session, 1);

				// Response: finished the game, or left
				sendUpdateState(_session, 2);

				// Achievement Counter
				pgi->sys_achieve.incrementCounter(0x6C400004u/*Normal game complete*/);

			}else if (m_game_init_state == 1 && _option == 1) {	// Time is up

				requestFinishHole(_session, 1);		// Time is up

				// Shows msg that the player finished the game
				sendFinishMessage(_session);

				// Response: hole finished
				updateFinishHole(_session, 0);

				// Response for Tourney time is up
				sendTimeIsOver(_session);
			}
		}

		setGameFlag(pgi, (_option == 0) ? PlayerGameInfo::eFLAG_GAME::FINISH : PlayerGameInfo::eFLAG_GAME::END_GAME);
		
		GetLocalTime(&pgi->time_finish);

		if (AllCompleteGameAndClear() && m_game_init_state == 1)
			finish();	// Sends the packets that end the game, e.g.: 0xCE, 0x79, etc
	}
}

void GrandPrix::startTime(void* _quem) {

	try {
		
		if (_quem != nullptr && m_gp.time_hole > 0) {

			player *p = reinterpret_cast< player* >(_quem);

			// Stops the timer if there is already one
			auto timer = m_timer_manager.findTimer(p);

			// There is no timer created yet, creates one for it
			if (timer == nullptr || timer->m_timer == nullptr) {

				if (timer == nullptr && (timer = m_timer_manager.insertTimer(p, nullptr)) == nullptr)
					throw exception("[GrandPrix::startTime][Error] Player[UID=" + std::to_string(p->m_pi.uid) 
							+ "] nao conseguiu criar um timer_ctx para poder criar um timer para o player. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 1050, 0));

				job j(GrandPrix::end_time, this, _quem);

				timer->m_timer = sgs::gs::getInstance().makeTime(m_gp.time_hole * 1000/*milliseconds*/, j);	// already in minutes milliseconds

#ifdef _DEBUG
				_smp::message_pool::getInstance().push(new message("[GrandPrix::startTime][Log] Criou o Timer[Tempo=" + std::to_string(m_gp.time_hole) + "seg"
						+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] para o Player[UID=" + std::to_string(p->m_pi.uid) + "].", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
			
			}else {

				// There is already a timer, resets it and starts again
				if (timer->m_timer != nullptr) {
					
					if (timer->m_timer->getState() != timer::TIMER_STATE::STOPPED)
						timer->m_timer->stop();

					// starts it again
					timer->m_timer->start();

#ifdef _DEBUG
					_smp::message_pool::getInstance().push(new message("[GrandPrix::startTime][Log] Reiniciou o Timer[Tempo=" + std::to_string(m_gp.time_hole) + "seg"
							+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] para o Player[UID=" + std::to_string(p->m_pi.uid) + "].", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
				}
			}

		}

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::startTime][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

bool GrandPrix::stopTime(void* _quem) {

	bool ret = true;

	try {

		if (_quem != nullptr) {

			player *p = reinterpret_cast< player* >(_quem);

			auto timer = m_timer_manager.findTimer(p);

			if (timer != nullptr && timer->m_timer != nullptr && timer->m_timer->getState() != timer::TIMER_STATE::STOPPED) {
				
				timer->m_timer->stop();

#ifdef _DEBUG
				_smp::message_pool::getInstance().push(new message("[GrandPrix::stopTime][Log] Parou o Timer[Tempo=" + std::to_string(m_gp.time_hole) + "seg"
						+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] para o Player[UID=" + std::to_string(p->m_pi.uid) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
			}
		}

	}catch (exception& e) {

		ret = false;

		_smp::message_pool::getInstance().push(new message("[GrandPrix::stopTimer][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return ret;
}

void GrandPrix::timeIsOver(void* _quem) {

	try {

		if (_quem != nullptr) {

			player *s = reinterpret_cast< player* >(_quem);

			try {

				// Locker Player
				m_lock_manager.lock(s);

				auto timer = m_timer_manager.findTimer(s);

				if (timer != nullptr && timer->m_timer != nullptr) {

					// Stops the timer if it is not stopped
					if (timer->m_timer->getState() != timer::TIMER_STATE::STOPPED)
						timer->m_timer->stop();

					// Updates the player's data: he gave up because his hole time ran out
					INIT_PLAYER_INFO("timeIsOver", "acabou o tempo do hole do player", s);

					// Player has not finished the hole yet
					if (pgi->finish_hole2 == 0u && pgi->finish_hole3 == 0u) {
						
						auto hole = m_course->findHole(pgi->hole);

						if (hole == nullptr)
							throw exception("[GrandPrix::timeIsOver][Error] player[UID=" + std::to_string(s->m_pi.uid) + "] tentou pegar hole[NUMERO="
									+ std::to_string((unsigned short)pgi->hole) + "] no jogo, mas o numero do hole is invalid. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 1020, 0));

						pgi->data.tacada_num = hole->getPar().total_shot;	// Give up
				
						// Timed out
						pgi->data.time_out = 1;
						//pgi->data.giveup = 1;

		#ifdef _DEBUG
						_smp::message_pool::getInstance().push(new message("[GrandPrix::stopTime][Log] Acabou o tempo do hole Timer[Tempo=" + std::to_string(m_gp.time_hole) + "seg"
								+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] do Player[UID=" + std::to_string(s->m_pi.uid) + "]", CL_FILE_LOG_AND_CONSOLE));
		#endif // _DEBUG

						// Sends to the player that the hole time is up
						packet p((unsigned short)0x259);

						p.addUint32(0);	// OK

						packet_func::session_send(p, s, 1);
					}
				}

				// Release
				m_lock_manager.unlock(s);

			}catch (exception& e) {
				UNREFERENCED_PARAMETER(e);

				// Release
				m_lock_manager.unlock(s);

				// Rethrows so the outer try..catch shows the message in the log
				throw;
			}

		}

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::timeIsOver][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::requestCalculeRankPlace() {

	if (!m_player_order.empty())
		m_player_order.clear();

	for (auto& el : m_player_info)
		if (el.second->flag != PlayerGameInfo::eFLAG_GAME::QUIT)	// except those who quit
			m_player_order.push_back(el.second);

	// Add the Bots
	for (auto& el : m_bot)
		m_player_order.push_back(&el.pi);

	std::sort(m_player_order.begin(), m_player_order.end(), Game::sort_player_rank);
}

bool GrandPrix::init_game() {
	
	if (m_players.size() > 0) {

		// variable that stores the system local date
		initGameTime();

		m_game_init_state = 1;	// Started

		m_grand_prix_state = true;
	}

	return true;
}

int GrandPrix::checkEndShotOfHole(player& _session) {

	// Now checks whether he finished the hole and such
	INIT_PLAYER_INFO("checkEndShotOfHole", "tentou verificar a ultima tacada do hole no jogo", &_session);

	if (pgi->shot_sync.state_shot.display.stDisplay.acerto_hole || pgi->data.giveup) {

		if (pgi->data.bad_condute >= 3) {	// Kicks the player, he gave up 3 times

			// !!@@@
			// Removes the player from the room
			return 2;
		}

		// Checks whether the player finished the game, did the last hole
		if (m_course->findHoleSeq(pgi->hole) == m_ri.qntd_hole) {

			// Response for the player who finished the last hole of the Game
			packet p((unsigned short)0x199);

			packet_func::session_send(p, &_session, 1);

			// Did the Last Hole, calculates Clear Bonus for the player
			if (pgi->shot_sync.state_shot.display.stDisplay.clear_bonus) {

				if (!sMap::getInstance().isLoad())
					sMap::getInstance().load();

				auto map = sMap::getInstance().getMap(m_ri.course & 0x7F);

				if (map == nullptr)
					_smp::message_pool::getInstance().push(new message("[GrandPrix::checkEndShotOfHole][Error][WARNING] tentou pegar o Map dados estaticos do course[COURSE="
							+ std::to_string((unsigned short)(m_ri.course & 0x7F)) + "], mas nao conseguiu encontra na classe do Server.", CL_FILE_LOG_AND_CONSOLE));
				else
					pgi->data.bonus_pang += sMap::getInstance().calculeClear30s(*map, m_ri.qntd_hole);
			}
		}

		finishHole(_session);

		changeHole(_session);
	
	}else
		clearAllShotPacket(_session);

	return 0;
}

void GrandPrix::requestTranslateSyncShotData(player& _session, ShotSyncData& _ssd) {
	CHECK_SESSION_BEGIN("requestTranslateSyncShotData");

	try {

		// !@ Test
		//Sleep(5000);

		auto s = findSessionByOID(_ssd.oid);

		if (s == nullptr)
			throw exception("[GrandPrix::requestTranslateSyncShotData][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou sincronizar tacada do player[OID="
					+ std::to_string(_ssd.oid) + "], mas o player nao existe nessa jogo. Hacker ou Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 200, 0));

		// Blocks the player for the time
		m_lock_manager.lock(&_session);

		// Update Sync Shot Player
		if (_session.m_pi.uid == s->m_pi.uid) {

			INIT_PLAYER_INFO("requestTranslateSyncShotData", "tentou sincronizar a tacada no jogo", &_session);

			pgi->shot_sync = _ssd;

			// Last Location Player
			auto last_location = pgi->location;

			// Update Location Player
			pgi->location.x = _ssd.location.x;
			pgi->location.z = _ssd.location.z;

			// Update Pang and Bonus Pang
			pgi->data.pang = _ssd.pang;
			pgi->data.bonus_pang = _ssd.bonus_pang;

			// Already only in the function that starts the player's turn time
			pgi->data.tacada_num++;

			if (_ssd.state == ShotSyncData::OUT_OF_BOUNDS || _ssd.state == ShotSyncData::UNPLAYABLE_AREA)
				pgi->data.tacada_num++;

			// Checks whether the Grand Prix has special rules and whether the rule forbids special shots
			// If so, the penalty is +1 on the player's shot
			if (m_gp.rule == eRULE::SPECIAL_SHOT && _ssd.state_shot.display.stDisplay.special_shot)
				pgi->data.penalidade++;

			// Hole find
			auto hole = m_course->findHole(pgi->hole);

			if (hole == nullptr)
				throw exception("[GrandPrix::requestTranslateSyncShotData][Error] player[UID=" + std::to_string(_session.m_pi.uid) + "] tentou sincronizar tacada no hole[NUMERO="
						+ std::to_string((unsigned short)pgi->hole) + "], mas o numero do hole is invalid. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 12, 0));

			// Counts the next shot already, on give up
			if (!_ssd.state_shot.display.stDisplay.acerto_hole && hole->getPar().total_shot <= (pgi->data.tacada_num + 1)) {

				// +1 for give up, only add if the number of shots was not exceeded
				if (pgi->data.tacada_num < hole->getPar().total_shot)
					pgi->data.tacada_num++;

				pgi->data.giveup = 1;

				// Adds +1 to Bad Conduct
  				pgi->data.bad_condute++;
			}

			// The hole is over for the player's hole time
			if (_ssd.state_shot.display.stDisplay.acerto_hole || pgi->data.giveup) {

				// sets the player's finish hole2 flag
				pgi->finish_hole2 = 1u;

				// Stops the player's time
				stopTime(&_session);
				stopTimeRule(&_session);
			}

			// achievements for power shot, int32_t putt, beam impact, etc here
			update_sync_shot_achievement(_session, last_location);
		}

		// Release
		m_lock_manager.unlock(&_session);

	}catch (exception& e) {

		// Release
		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestTranslateSyncShotData][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::init_bots() {

	// Achievement Bosts
	uint32_t bots_counter_typeid = 0u;

	if (m_players.size() == 30)
		bots_counter_typeid = 0x6C4000B8u;	// No AI, All players
	else if (m_players.size() == 1)
		bots_counter_typeid = 0x6C4000B7u;	// All AI, 1 player

	if (bots_counter_typeid > 0u) {

		for (auto& el : m_players) {

			INIT_PLAYER_INFO("GrandPrix", "tentou inicializar o counter item do Grand Prix Bots", el);

			pgi->sys_achieve.incrementCounter(bots_counter_typeid);
		}
	}

	// Initializes the bots
	if (m_players.size() < 30) {

		// Average score (Avg. Score) of the room's player(s)
		auto mediaScoreAllPlayerRoom = std::accumulate(m_players.begin(), m_players.end(), 0.f, [](float _sum, player* _player) -> float {
			
			// invalid player, skips it
			if (_player == nullptr)
				return _sum;


			return _sum + _player->m_pi.ui.getMediaScore();
		}) / m_players.size();

		// Lambda function that updates the bot score based on the room's Avg.Score
		auto lambdaBotScoreByFactorAvgScoreRoom = [](IFF::GrandPrixData& _gp, float _room_avg_score) -> IFF::GrandPrixData::BOT {

			IFF::GrandPrixData::BOT bot;

			unsigned char qntd_hole = _gp.course_info.qntd_hole == 0 ? 18 : _gp.course_info.qntd_hole;
			float media_bot = ((_gp.bot.score_min + _gp.bot.score_med + _gp.bot.score_max) / 3.f) * 1.7f;
			float media_score_por_hole = (((18.f / qntd_hole) * media_bot + 72) - _room_avg_score + 180.f) / 180.f;

			auto bySign = [&media_score_por_hole](int32_t _score) -> int {

				if (_score == 0)
					return media_score_por_hole <= 0.8f ? 1 : (media_score_por_hole >= 1.4f ? -1 : _score);

				if (_score < 0)
					return (int)std::round(_score * media_score_por_hole);
				
				return (int)std::round(_score / (media_score_por_hole == 0.0f ? 0.001f : media_score_por_hole));
			};

			bot.score_min = bySign(_gp.bot.score_min);
			bot.score_med = bySign(_gp.bot.score_med);
			bot.score_max = bySign(_gp.bot.score_max);

			return bot;
		};

		auto bot_score = lambdaBotScoreByFactorAvgScoreRoom(m_gp, mediaScoreAllPlayerRoom);

#ifdef _DEBUG
		_smp::message_pool::getInstance().push(new message("[GrandPrix::init_bots][Log] Media Score da sala: " + std::to_string(mediaScoreAllPlayerRoom) 
				+ "\nBOT SCORE ORIGINAL [MIN: " + std::to_string(m_gp.bot.score_min) + ", MED: " + std::to_string(m_gp.bot.score_med) + ", MAX: " + std::to_string(m_gp.bot.score_max) 
				+ "], BOT BY FACTOR SCORE [MIN: " + std::to_string(bot_score.score_min) + ", MED: " + std::to_string(bot_score.score_med) + ", MAX: " + std::to_string(bot_score.score_max) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif

		auto qntd = 30u - m_players.size();

		auto gp_ai = sIff::getInstance().getGrandPrixAIOptionalData();

		Lottery lottery((uint64_t)this);

		for (auto& el : gp_ai) {
			
			if (el.second.active && el.second._class == m_gp._class)
				lottery.push(1000u, el.second.id);

		}

		// Checks whether there are enough bots to draw
		if ((lottery.getLimitProbilidade() / 1000u) < qntd) {

			auto rest_qntd = (qntd - (lottery.getLimitProbilidade() / 1000u));

			_smp::message_pool::getInstance().push(new message("[GrandPrix::init_bots][WARNING] GrandPrix[TYPEID=" + std::to_string(m_gp._typeid) + ", CLASS=" + std::to_string(m_gp._class) 
					+ "] nao tem todos o bot necessarios[QNTD_REQ=" + std::to_string(qntd) + ", QNTD_LEFT=" + std::to_string(rest_qntd) + "] nessa class pega de outra.", CL_FILE_LOG_AND_CONSOLE));
			
			for (auto& el : gp_ai) {

				if (el.second.active && el.second._class != m_gp._class) {
					lottery.push(1000u, el.second.id);

					if (--rest_qntd == 0)
						break;
				}
			}
		}

		// Draws the Bots and configures them
		Lottery::LotteryCtx *lc = nullptr;
		Lottery lottery_score((uint64_t)this);

		PlayerGameInfo tmp_pi{ 0 };

		Hole *hole = nullptr;

		Bot bot{ 0 };

		int32_t score = 0, min_shot = 0, diff_min_shot = 0, diff_max_shot = 0;
		uint64_t pang = 0u;
		uint64_t bonus_pang = 0u;

		// Bot average if he makes par on all holes
		float media_all_par_hole = m_course->getMediaAllParHolesBySeq(m_ri.qntd_hole);

		// probability multiplier depending on the wind and type
		auto lambdaWindFactor = [](Hole& _hole, Bot::eTYPE_SCORE _type, bool _same_type) -> uint32_t {

			uint32_t factor = 1u;

			if (_hole.getWind().wind >= 0 && _hole.getWind().wind < 3 && _type == Bot::eTYPE_SCORE::MAX_SCORE)		// weak
				factor = 2u;
			else if (_hole.getWind().wind >= 3 && _hole.getWind().wind < 6 && _type == Bot::eTYPE_SCORE::MED_SCORE)	// medium
				factor = 4u;
			else if (_hole.getWind().wind >= 6 && _hole.getWind().wind < 8 && _type == Bot::eTYPE_SCORE::MIN_SCORE)	// strong
				factor = 6u;
			else if (_hole.getWind().wind >= 8 && _type == Bot::eTYPE_SCORE::MIN_SCORE)								// super wind
				factor = 7u;

			// Rain or Snow
			if (_hole.getWeather() == 2 && (_type == Bot::eTYPE_SCORE::MED_SCORE || _type == Bot::eTYPE_SCORE::MIN_SCORE))
				factor += 2u;

			if (_same_type)
				factor += 2u;

			return factor;
		};

		for (auto i = 0u; i < qntd; ++i) {

			if ((lc = lottery.spinRoleta(true)) != nullptr) {

				// Clear Bot for new data
				bot.clear();

				bot.id = (uint32_t)lc->value;

				bot.type_score = (sRandomGen::getInstance().rIbeMt19937_64_chrono() % 5 == 0
									? Bot::eTYPE_SCORE::MAX_SCORE
									: (sRandomGen::getInstance().rIbeMt19937_64_chrono() % 3 == 0
										? Bot::eTYPE_SCORE::MED_SCORE
										: Bot::eTYPE_SCORE::MIN_SCORE
										)
									);

				bot.max_record = (bot.type_score == Bot::eTYPE_SCORE::MAX_SCORE 
									? bot_score.score_max + (int)(sRandomGen::getInstance().rIbeMt19937_64_chrono() % 3)				// This one can vary 3 down
									: (bot.type_score == Bot::eTYPE_SCORE::MED_SCORE
										? bot_score.score_med + (int)(sRandomGen::getInstance().rIbeMt19937_64_chronoRange(0, 6) - 3)	// This one can vary 3 up and 3 down
										: bot_score.score_min + (int)(sRandomGen::getInstance().rIbeMt19937_64_chronoRange(0, 5) - 3)	// This one can vary 2 up and 3 down
									  )
								);

				bot.qntd_hole = m_ri.qntd_hole;

				for (auto j = 1u; j <= bot.qntd_hole; ++j) {
					
					if ((hole = m_course->findHoleBySeq((unsigned short)j)) != nullptr) {

						// Score
						bot.med_shot_per_hole = (int)std::round(((bot.qntd_hole - j + 1) * media_all_par_hole + (bot.max_record - bot.record)) / (float)(bot.qntd_hole - j + 1));

						min_shot = (hole->getPar().par + ((m_ri.natural.stBit.short_game) ? -2/*Short Game*/ : hole->getPar().range_score[0]));

						if (min_shot >= bot.med_shot_per_hole)	// Lower score limit of the hole
							score = min_shot - hole->getPar().par;
						else if (bot.med_shot_per_hole >= hole->getPar().total_shot) // Upper score limit of the hole
							score = hole->getPar().total_shot - hole->getPar().par;
						else {

							lottery_score.clear();

							// Margin available to make a better score
							diff_min_shot = (bot.med_shot_per_hole - min_shot);

							// Margin available to make a worse score
							diff_max_shot = (hole->getPar().total_shot - bot.med_shot_per_hole);

							if (bot.med_shot_per_hole < hole->getPar().par) {

								// min shot, max score
								lottery_score.push
								(
									1000u * diff_max_shot * lambdaWindFactor
									(
										*hole,
										Bot::eTYPE_SCORE::MAX_SCORE,
										bot.type_score == Bot::eTYPE_SCORE::MAX_SCORE
									),
									Bot::eTYPE_SCORE::MAX_SCORE
								);
							}

							// med
							lottery_score.push
							(
								1000u * bot.med_shot_per_hole * lambdaWindFactor
								(
									*hole,
									Bot::eTYPE_SCORE::MED_SCORE,
									bot.type_score == Bot::eTYPE_SCORE::MED_SCORE
								),
								Bot::eTYPE_SCORE::MED_SCORE
							);

							// max shot, min score
							lottery_score.push
							(
								1000u * diff_min_shot * lambdaWindFactor
								(
									*hole, 
									Bot::eTYPE_SCORE::MIN_SCORE, 
									bot.type_score == Bot::eTYPE_SCORE::MIN_SCORE
								), 
								Bot::eTYPE_SCORE::MIN_SCORE
							);

							if ((lc = lottery_score.spinRoleta(true)) == nullptr) {
								
								_smp::message_pool::getInstance().push(new message("[GrandPrix::init_bots][WARNING] nao conseguiu rodar a roleta para o score do bot, usando o med_shot_per_hole.", CL_FILE_LOG_AND_CONSOLE));

								score = bot.med_shot_per_hole - hole->getPar().par;
							
							}else {

								if (lc->value == Bot::eTYPE_SCORE::MAX_SCORE)
									score = (bot.med_shot_per_hole - (int32_t)sRandomGen::getInstance().rIbeMt19937_64_chronoRange(0, diff_min_shot)) - hole->getPar().par;
								else if (lc->value == Bot::eTYPE_SCORE::MED_SCORE)
									score = bot.med_shot_per_hole - hole->getPar().par;
								else
									score = (bot.med_shot_per_hole + (int32_t)sRandomGen::getInstance().rIbeMt19937_64_chronoRange(0, diff_max_shot)) - hole->getPar().par;
							}
						}

						// Pang and Bonus pang
						pang = sRandomGen::getInstance().rIbeMt19937_64_chrono() % (351ull * (hole->getWeather() == 2 ? 2 : 1));
						bonus_pang = sRandomGen::getInstance().rIbeMt19937_64_chrono() % 200ull;

						// Inserts the Bot's Hole (i)
						bot.hole.push_back(Bot::Hole(m_ri.course & 0x7Fu, hole->getNumero(), score, pang, bonus_pang));

						// Increments the total
						bot.record += score;
						bot.pang_total += pang;
						bot.bonus_pang_total += bonus_pang;
					
					} // If course->findHole

				}	// For Hole Bot

				if (bot.qntd_hole != (unsigned char)bot.hole.size())
					_smp::message_pool::getInstance().push(new message("[GrandPrix::init_bots][WARNIG] Bot[ID=" + std::to_string(bot.id) 
							+ ", HOLE_QNTD_INIT=" + std::to_string(bot.hole.size()) + ", HOLE_QNTD_GP=" + std::to_string((unsigned short)bot.qntd_hole)
							+ "] qntd de holes inicializado esta diferente da quantidade de holes da sala Grand Prix. Bug", CL_FILE_LOG_AND_CONSOLE));

				tmp_pi.clear();

				tmp_pi.flag = PlayerGameInfo::eFLAG_GAME::BOT;
				tmp_pi.data.score = bot.record;
				tmp_pi.data.pang = bot.pang_total;
				tmp_pi.data.bonus_pang = bot.bonus_pang_total;

				bot.pi = tmp_pi;

#ifdef _DEBUG
				_smp::message_pool::getInstance().push(new message("[GrandPrix::init_bots][Log] Bot[ID="
						+ std::to_string(bot.id) + ", MAX_RECORD: " + std::to_string(bot.max_record) + ", RECORD: " + std::to_string(bot.record) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif

				// Add bot to the vector
				m_bot.push_back(bot);

			}	// If lottery.spinRoleta

		}	// For Rest of Bot

		// Sort Bot
		// Sorts the Bots by lowest record and highest pang
		std::sort(m_bot.begin(), m_bot.end(), [](auto& _1, auto& _2) {

			if (_1.record == _2.record)
				return _1.pang_total > _2.pang_total;

			return _1.record < _2.record;
		});

	} // If players.size < 30
}

void GrandPrix::consomeTicket() {

	WarehouseItemEx *pWi = nullptr;

	for (auto& el : m_players) {
		
		try {

			// Removes the Grand Prix ticket from the player
			pWi = el->m_pi.findWarehouseItemByTypeid(m_gp.ticket._typeid);

			if (pWi == nullptr)
				throw exception("[GrandPrix::consomeTicket][Error] player[UID=" + std::to_string(el->m_pi.uid)
					+ "] tentou comecar o jogo na sala[NUMERO=" + std::to_string(m_ri.numero) + ", MASTER=" + std::to_string(m_ri.master)
					+ "], mas o player nao tem o Ticket[TYPEID=" + std::to_string(m_gp.ticket._typeid)
					+ "] para jogar o Grand Prix. Hacker ou Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 9, 0x5900203));

			if (pWi->STDA_C_ITEM_QNTD < (short)m_gp.ticket.qntd)
				throw exception("[GrandPrix::consomeTicket][Error] player[UID=" + std::to_string(el->m_pi.uid)
					+ "] tentou comecar o jogo na sala[NUMERO=" + std::to_string(m_ri.numero) + ", MASTER=" + std::to_string(m_ri.master)
					+ "], mas o player nao tem a quantidade de Ticket[TYPEID=" + std::to_string(m_gp.ticket._typeid)
					+ ", REQ_QNTD=" + std::to_string(m_gp.ticket.qntd) + ", HAVE_QNTD=" + std::to_string(pWi->STDA_C_ITEM_QNTD)
					+ "] para jogar o Grand Prix. Hacker ou Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 10, 0x5900203));

			stItem item{ 0 };

			item.type = 2;
			item.id = pWi->id;
			item._typeid = pWi->_typeid;
			item.qntd = m_gp.ticket.qntd;
			item.STDA_C_ITEM_QNTD = (short)item.qntd * -1;

			// Update On Server And Database
			if (item_manager::removeItem(item, *el) <= 0)
				throw exception("[GrandPrix::consomeTicket][Error] player[UID=" + std::to_string(el->m_pi.uid)
					+ "] tentou comecar o jogo na sala[NUMERO=" + std::to_string(m_ri.numero) + ", MASTER=" + std::to_string(m_ri.master)
					+ "], mas nao conseguiu excluir o Ticket[TYPEID=" + std::to_string(item._typeid) + "] do player. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 11, 0x5900203));


			// Update the player's Grand Prix Ticket in the game
			packet p((unsigned short)0x216);

			p.addUint32((const uint32_t)GetSystemTimeAsUnix());
			p.addUint32(1);	// Count

			p.addUint8(item.type);
			p.addUint32(item._typeid);
			p.addInt32(item.id);
			p.addUint32(item.flag_time);
			p.addBuffer(&item.stat, sizeof(item.stat));
			p.addUint32((item.STDA_C_ITEM_TIME > 0) ? item.STDA_C_ITEM_TIME : item.STDA_C_ITEM_QNTD);
			p.addZeroByte(25);

			packet_func::session_send(p, el, 1);
	
		}catch (exception& e) {

			_smp::message_pool::getInstance().push(new message("[GrandPrix::consomeTicket][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));

			if (STDA_SOURCE_ERROR_DECODE(e.getCodeError()) != STDA_ERROR_TYPE::GRAND_PRIX)
				throw;
		}

	}
}

void GrandPrix::requestFinishExpGame() {

	if (m_players.size() > 0) {

		player *_session = nullptr;
		float stars = m_course->getStar();
		int32_t exp = 0;

		// Default hole exp of the Grand Prix
		switch (m_ri.qntd_hole) {
		case 3:
			exp = 6;
			break;
		case 6:
			exp = 11;
			break;
		case 9:
			exp = 17;
			break;
		case 18:
			exp = 22;
			break;
		default:
			exp = 7;
			break;
		}

		stars = (stars < 1.1f) ? 1.1f : stars;

		stars = ((stars - 1.1f) * 0.375f) + 1.0f;

		exp = (int)(exp * stars);

		// Grand Prix Rookie gives a little less experience
		if (sIff::getInstance().getGrandPrixAba(m_gp._typeid) == IFF::GrandPrixData::GP_ABA::ROOKIE && sIff::getInstance().isGrandPrixNormal(m_gp._typeid))
			exp = (int)(exp * 0.92f);

		for (auto i = 0u; i < m_player_order.size(); ++i) {

			if (m_player_order[i]->flag != PlayerGameInfo::eFLAG_GAME::BOT 
				&& m_player_order[i]->flag == PlayerGameInfo::eFLAG_GAME::FINISH 
				&& (_session = findSessionByUID(m_player_order[i]->uid)) != nullptr) {

				// Player and server rate
				exp = (int)(exp * TRANSF_SERVER_RATE_VALUE(m_player_order[i]->used_item.rate.exp) * TRANSF_SERVER_RATE_VALUE(m_rv.exp));

				// Exp the player earned
				if (m_player_order[i]->level < 70/*Last level does not gain exp*/)
					m_player_order[i]->data.exp = exp;

				_smp::message_pool::getInstance().push(new message("[GrandPrix::requestFinishExpGame][Log] player[UID=" + std::to_string(m_player_order[i]->uid)
						+ "] ganhou " + std::to_string(m_player_order[i]->data.exp) + " de experience.", CL_FILE_LOG_AND_CONSOLE));
			}
		}
	}
}

void GrandPrix::requestMakeRankPlayerDisplayCharacter() {

	if (m_player_order.size() <= 0)
		requestCalculeRankPlace();

	RankPlayerDisplayChracter rpdc{ 0 };
	
	player *p = nullptr;

	// Top 3
	for (auto i = 0u; i < m_player_order.size() && i < 3u; ++i) {

		if (m_player_order[i]->flag != PlayerGameInfo::eFLAG_GAME::BOT) {

			if ((p = findSessionByUID(m_player_order[i]->uid)) != nullptr) {
				
				rpdc.clear();

				rpdc.uid = p->m_pi.uid;
				rpdc.rank = i + 1;

				if (p->m_pi.ei.char_info != nullptr) {
					
					rpdc.default_hair = p->m_pi.ei.char_info->default_hair;
					rpdc.default_shirts = p->m_pi.ei.char_info->default_shirts;

#if defined(_WIN32)
					memcpy_s(rpdc.parts_typeid, sizeof(rpdc.parts_typeid), p->m_pi.ei.char_info->parts_typeid, sizeof(rpdc.parts_typeid));
					memcpy_s(rpdc.auxparts, sizeof(rpdc.auxparts), p->m_pi.ei.char_info->auxparts, sizeof(rpdc.auxparts));
					memcpy_s(rpdc.parts_id, sizeof(rpdc.parts_id), p->m_pi.ei.char_info->parts_id, sizeof(rpdc.parts_id));
#elif defined(__linux__)
					memcpy(rpdc.parts_typeid, p->m_pi.ei.char_info->parts_typeid, sizeof(rpdc.parts_typeid));
					memcpy(rpdc.auxparts, p->m_pi.ei.char_info->auxparts, sizeof(rpdc.auxparts));
					memcpy(rpdc.parts_id, p->m_pi.ei.char_info->parts_id, sizeof(rpdc.parts_id));
#endif
				}

				// Add to the vector
				m_rank_player_display_char.push_back(rpdc);
			}
		}
	}

	// Sorts from lowest to highest
	std::sort(m_rank_player_display_char.begin(), m_rank_player_display_char.end(), [](auto& _1, auto& _2) {
		return _1.rank < _2.rank;
	});
}

void GrandPrix::finish() {

	m_game_init_state = 2;	// Finished

	// Already includes the bots, I overrode this function
	requestCalculeRankPlace();

	requestMakeRankPlayerDisplayCharacter();

	requestFinishExpGame();

	for (auto& el : m_players) {

		INIT_PLAYER_INFO("finish", "tentou finalizar os dados do jogador no jogo", el);

		if (pgi->flag != PlayerGameInfo::eFLAG_GAME::QUIT)
			requestFinishData(*el);
	}
}

void GrandPrix::requestFinishData(player& _session) {

	// Finish Artefact Frozen Flame is now handled directly in Finish Item Used Game
	requestFinishItemUsedGame(_session);

	requestSaveDrop(_session);

	rain_hole_consecutivos_count(_session);			// counts the rain achievements on consecutive holes

	score_consecutivos_count(_session);				// counts the player's back-to-back achievements (2 or more equal consecutive scores)

	rain_count(_session);							// Rain count achievement here

	achievement_top_3_1st(_session);				// If the Player placed in the Top 3 add +1 to the top 3 counter, and if he placed first add +1 to the first place counter

	INIT_PLAYER_INFO("requestFinishData", "tentou finalizar dados do jogo", &_session);

	// Response game finished - Drop Items
	sendDropItem(_session);

	// Response game finished - Scoreboard
	sendPlacar(_session);

	// Here are the 3 podium players, but bot players are not included
	sendRankPlayerDisplayCharacter(_session);

	// Trophy the player won
	sendTrofel(_session);

	// Sends the prizes the player won in the Grand Prix
	sendRewardRankAndGrandPrix(_session);

	// Checks whether the player completed this Grand Prix in a better position, 
	// if so, updates on the server, db and in the game
	requestSaveGrandPrixClear(_session);
}

void GrandPrix::requestSaveGrandPrixClear(player& _session) {

	try {

		if (m_player_order.size() <= 0)
			requestCalculeRankPlace();

		auto it = std::find_if(m_player_order.begin(), m_player_order.end(), [&](auto& _el) {
			return _el->uid == _session.m_pi.uid;
		});

		auto position = std::distance(m_player_order.begin(), it) + 1;

		// Updates the player's Grand Prix Clear
		if (_session.m_pi.updateGrandPrixClear(m_gp.typeid_link, (int)position)) {

			// Updated, the player placed in a better position in the Grand Prix
			_smp::message_pool::getInstance().push(new message("[GrandPrix::requestSaveGrandPrixClear][Log] Player[UID=" + std::to_string(position) 
					+ "] ficou em um posicao melhor no Grand Prix[TYPEID=" + std::to_string(m_gp._typeid) + ", TYPEID_LINK=" 
					+ std::to_string(m_gp.typeid_link) + ", POSITION=" + std::to_string(position) + "].", CL_FILE_LOG_AND_CONSOLE));

			// Update On Game
			packet p((unsigned short)0x25A);

			p.addUint32(0);	// OK;

			p.addUint32(m_gp.typeid_link);
			p.addUint32((uint32_t)position);

			packet_func::session_send(p, &_session, 1);
		}
	
	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[requestSaveGrandPrixClear][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::sendTrofel(player& _session) {

	uint32_t all_player = getCountPlayersGame();

	uint32_t count_trofel = 0u, i = 0u;

	if (m_player_order.size() <= 0)
		requestCalculeRankPlace();

	if (m_player_order.size() != (all_player + m_bot.size())) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::sendTrofel][Error] VALUES[ORDER=" + std::to_string(m_player_order.size()) + ", INFO="
				+ std::to_string(m_player_info.size()) + ", BOT=" + std::to_string(m_bot.size())
				+ "] nao conseguiu gerar os trofeus por que o vector de player rank order nao bate com o dos players no jogo", CL_FILE_LOG_AND_CONSOLE));

		return;
	}

	// Gives the Trophies to the first 3 Players, bots are excluded
	if (!m_gp_reward.empty()) {

		IFF::GrandPrixRankReward gprr{ 0 };
		stItem item{ 0 };

		packet p;

		auto it = std::find_if(m_player_order.begin(), m_player_order.end(), [&](auto& _el) {
			return _el->uid == _session.m_pi.uid;
		});

		if (it != m_player_order.end()) {

			try {

				// This one throws exception::out_of_range if the index does not exist
				gprr = m_gp_reward.at(std::distance(m_player_order.begin(), it));

				// Initializes the Trophy
				item.type = 2;
				item.id = -1;
				item._typeid = gprr.trophy_typeid;
				item.qntd = 1;
				item.STDA_C_ITEM_QNTD = (short)item.qntd;

				// Update on Server and Database
				if (item_manager::addItem(item, _session, 0, 0) >= item_manager::RetAddItem::TYPE::T_SUCCESS) {

					// Trophy successfully added to the player
					_smp::message_pool::getInstance().push(new message("[GrandPrix::sendTrofel][Log] Player[UID=" + std::to_string(_session.m_pi.uid) 
							+ "] ganhou Grand Prix Trofel[TYPEID=" + std::to_string(gprr.trophy_typeid) + "] na Posicao[RANK=" + std::to_string(gprr.rank) + "].", CL_FILE_LOG_AND_CONSOLE));

					// Update Trophy on Game
					p.init_plain((unsigned short)0x25C);

					p.addUint32(0);	// OK;

					p.addUint32(gprr.trophy_typeid);

					packet_func::session_send(p, &_session, 1);

					// Update Item on Game (Trophy)
					p.init_plain((unsigned short)0x216);

					p.addUint32((const uint32_t)GetSystemTimeAsUnix());
					p.addUint32(1u);	// Count

					p.addUint8(item.type);
					p.addUint32(item._typeid);
					p.addInt32(item.id);
					p.addUint32(item.flag_time);
					p.addBuffer(&item.stat, sizeof(item.stat));
					p.addUint32((item.STDA_C_ITEM_TIME > 0) ? item.STDA_C_ITEM_TIME : item.STDA_C_ITEM_QNTD);
					p.addZeroByte(25);

					packet_func::session_send(p, &_session, 1);

				}else
					_smp::message_pool::getInstance().push(new message("[GrandPrix::sendTrofel][Error] Player[UID=" + std::to_string(_session.m_pi.uid) 
							+ "] tentou adicionar Grand Prix Trofel[TYPEID=" + std::to_string(item._typeid) + "] na Posicao[RANK=" + std::to_string(gprr.rank) 
							+ "], mas nao conseguiu adicionar o item.", CL_FILE_LOG_AND_CONSOLE));

			}catch (std::out_of_range& e) {
				
				UNREFERENCED_PARAMETER(e);
			
			}catch (exception& e) {

				_smp::message_pool::getInstance().push(new message("[GrandPrix::sendTrofel][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
			}

		}else
			_smp::message_pool::getInstance().push(new message("[GrandPrix::sendTrofel][WARNING] Player[UID=" + std::to_string(_session.m_pi.uid) + "] nao esta no vector de player order.", CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::sendRankPlayerDisplayCharacter(player& _session) {

	// 3 Podium Players
	packet p((unsigned short)0x258);

	p.addUint32(0u); // OK

	p.addUint8((unsigned char)m_rank_player_display_char.size());	// count

	for (auto& el : m_rank_player_display_char)
		p.addBuffer(&el, sizeof(RankPlayerDisplayChracter));

	packet_func::session_send(p, &_session, 1);
}

void GrandPrix::sendRewardRankAndGrandPrix(player& _session) {

	if (m_player_order.size() <= 0)
		requestCalculeRankPlace();

	// Finds the player in the Game's rank vector
	auto it = std::find_if(m_player_order.begin(), m_player_order.end(), [&](auto& _el) {
		return _el->uid == _session.m_pi.uid;
	});

	if (it != m_player_order.end()) {

		std::vector< stItem > v_item;
		stItem item{ 0 };

		// Reward Grand Prix
		for (auto i = 0u; i < 5u; ++i) {

			if (m_gp.reward._typeid[i] != 0) {

				item.clear();

				item.type = 2;
				item.id = -1;
				item._typeid = m_gp.reward._typeid[i];

				if (m_gp.reward.time[i] > 0) {

					item.qntd = 1;
					item.STDA_C_ITEM_QNTD = 1;
					item.STDA_C_ITEM_TIME = (short)m_gp.reward.time[i]; // Time in days
					item.flag_time = 4;	// Days
					item.flag = 0x40;	// Days

				}else {

					item.qntd = m_gp.reward.qntd[i];
					item.STDA_C_ITEM_QNTD = (short)item.qntd;
				}

				// Checks whether the player already has the item; for a caddie item, checks for the caddie first and then for the caddie item
				if ((sIff::getInstance().IsCanOverlapped(item._typeid) && sIff::getInstance().getItemGroupIdentify(item._typeid) != iff::CAD_ITEM) || !_session.m_pi.ownerItem(item._typeid)) {
							
					if (item_manager::isSetItem(item._typeid)) {
								
						auto v_stItem = item_manager::getItemOfSetItem(_session, item._typeid, false, 1/*Does not check the Level*/);

						if (!v_stItem.empty()) {
							
							// Checks whether more than 1 item is allowed, and if not, checks that the player does not have the item
							for (auto& el : v_stItem)
								if ((sIff::getInstance().IsCanOverlapped(el._typeid) && sIff::getInstance().getItemGroupIdentify(el._typeid) != iff::CAD_ITEM) || !_session.m_pi.ownerItem(el._typeid))
									v_item.push_back(el);
							
						}

					}else {

						// Add the Normal Item
						v_item.push_back(item);
					}

				}
			}
		}

		// Reward Rank
		IFF::GrandPrixRankReward gprr{ 0 };

		try {

			// Throws exception::out_of_range if the rank index does not exist
			gprr = m_gp_reward.at(std::distance(m_player_order.begin(), it));

			for (auto i = 0u; i < 5u; ++i) {

				if (gprr.reward._typeid[i] != 0) {

					item.clear();

					item.type = 2;
					item.id = -1;
					item._typeid = gprr.reward._typeid[i];

					if (gprr.reward.time[i] > 0) {

						item.qntd = 1;
						item.STDA_C_ITEM_QNTD = 1;
						item.STDA_C_ITEM_TIME = (short)gprr.reward.time[i];	// Time in days
						item.flag_time = 4;	// Days
						item.flag = 0x40;	// Days

					}else {

						item.qntd = gprr.reward.qntd[i];
						item.STDA_C_ITEM_QNTD = (short)item.qntd;
					}

					// Checks whether the player already has the item; for a caddie item, checks for the caddie first and then for the caddie item
					if ((sIff::getInstance().IsCanOverlapped(item._typeid) && sIff::getInstance().getItemGroupIdentify(item._typeid) != iff::CAD_ITEM) || !_session.m_pi.ownerItem(item._typeid)) {
							
						if (item_manager::isSetItem(item._typeid)) {
								
							auto v_stItem = item_manager::getItemOfSetItem(_session, item._typeid, false, 1/*Does not check the Level*/);

							if (!v_stItem.empty()) {
							
								// Checks whether more than 1 item is allowed, and if not, checks that the player does not have the item
								for (auto& el : v_stItem)
									if ((sIff::getInstance().IsCanOverlapped(el._typeid) && sIff::getInstance().getItemGroupIdentify(el._typeid) != iff::CAD_ITEM) || !_session.m_pi.ownerItem(el._typeid))
										v_item.push_back(el);
							
							}

						}else {

							// Add the Normal Item
							v_item.push_back(item);
						}

					}
				}
			}

		}catch (std::out_of_range& e) {
			UNREFERENCED_PARAMETER(e);
		}

		if (!v_item.empty()) {

			// Update Item on Server And Database
			item_manager::RetAddItem rai = item_manager::addItem(v_item, _session, 0, 0);

			if (rai.fails.size() > 0 && rai.type != item_manager::RetAddItem::T_SUCCESS_PANG_AND_EXP_AND_CP_POUCH)
				_smp::message_pool::getInstance().push(new message("[GrandPrix:sendRewardRankAndGrandPrix][WARNIG] nao conseguiu adicionar os Grand Prix Reward itens. Bug", CL_FILE_LOG_AND_CONSOLE));

			// Update on Game
			packet p((unsigned short)0x216);

			p.addUint32((const uint32_t)GetSystemTimeAsUnix());
			p.addUint32((uint32_t)v_item.size());	// Count

			for (auto& el : v_item) {
				p.addUint8(el.type);
				p.addUint32(el._typeid);
				p.addInt32(el.id);
				p.addUint32(el.flag_time);
				p.addBuffer(&el.stat, sizeof(el.stat));
				p.addUint32((el.STDA_C_ITEM_TIME > 0) ? el.STDA_C_ITEM_TIME : el.STDA_C_ITEM_QNTD);
				p.addZeroByte(25);
			}

			packet_func::session_send(p, &_session, 1);
		}

	}else
		_smp::message_pool::getInstance().push(new message("[GrandPrix::sendRewardRankAndGrandPrix][WARNIG] Player[UID=" + std::to_string(_session.m_pi.uid) + "] nao esta no vector player order.", CL_FILE_LOG_AND_CONSOLE));
}

void GrandPrix::sendAllToNextHole() {

	// Change Hole All Finish Hole
	packet p((unsigned short)0x255);

	packet_func::game_broadcast(*this, p, 1);
}

int GrandPrix::changeTurn(player& _session) {

	try {

		if (checkAllShotPacket(_session)) {

			INIT_PLAYER_INFO("changeTurn", "tentou trocar o turno do player", &_session);

			// Now checks whether he finished the hole and such
			if (pgi->shot_sync.state_shot.display.stDisplay.acerto_hole || pgi->data.giveup || pgi->data.time_out) {

				if (pgi->data.bad_condute >= 3) {	// Kicks the player, he gave up 3 times

					// !!@@@
					// Removes the player from the room
					return 2;
				}

				// Checks whether the player finished the game, did the last hole
				if (m_course->findHoleSeq(pgi->hole) == m_ri.qntd_hole) {

					// Response for the player who finished the last hole of the Game
					packet p((unsigned short)0x199);

					packet_func::session_send(p, &_session, 1);

					// Did the Last Hole, calculates Clear Bonus for the player
					if (pgi->shot_sync.state_shot.display.stDisplay.clear_bonus) {

						if (!sMap::getInstance().isLoad())
							sMap::getInstance().load();

						auto map = sMap::getInstance().getMap(m_ri.course & 0x7F);

						if (map == nullptr)
							_smp::message_pool::getInstance().push(new message("[TourneyBase::checkEndShotOfHole][Error][WARNING] tentou pegar o Map dados estaticos do course[COURSE="
								+ std::to_string((unsigned short)(m_ri.course & 0x7F)) + "], mas nao conseguiu encontra na classe do Server.", CL_FILE_LOG_AND_CONSOLE));
						else
							pgi->data.bonus_pang += sMap::getInstance().calculeClear30s(*map, m_ri.qntd_hole);
					}
				}

				finishHole(_session);

				changeHole(_session);

			}else
				clearAllShotPacket(_session);

		} // Wait, he has not finished sending all the shot packets yet

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::changeTurn][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return 0;
}

bool GrandPrix::checkAllShotPacket(player& _session) {

	bool ret = false;

	try {
		
		INIT_PLAYER_INFO("checkAllShotPacket", "tentou verificar as flag de sincronizacao de tacada do player", &_session);

		m_lock_manager.lock(&_session);

		ret = ((pgi->init_shot && pgi->sync_shot_flag || pgi->data.time_out) && pgi->finish_shot);

		m_lock_manager.unlock(&_session);

	}catch (exception& e) {

		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::checkAllShotPacket][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return ret;
}

void GrandPrix::clearAllShotPacket(player& _session) {

	try {
		
		INIT_PLAYER_INFO("clearAllShotPacket", "tentou limpar as flag de sincronizacao de tacada do player", &_session);

		// Clears the shot variables
		m_lock_manager.lock(&_session);

		pgi->init_shot = 0u;
		pgi->sync_shot_flag = 0u;
		pgi->finish_shot = 0u;

		m_lock_manager.unlock(&_session);

	}catch (exception& e) {

		m_lock_manager.unlock(&_session);

		_smp::message_pool::getInstance().push(new message("[GrandPrix::clearAllShotPacket][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

bool GrandPrix::checkAllClearHole() {
	
	uint32_t count = 0u;

#if defined(_WIN32)
	EnterCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_lock(&m_cs_sync_shot);
#endif

	// Check
	std::for_each(m_players.begin(), m_players.end(), [&](auto& _el) {

		try {

			INIT_PLAYER_INFO("checkAllClearHole", "tentou verificar se todos os player terminaram o hole no jogo", _el);

			if (pgi->finish_hole)
				count++;

		}catch (exception& e) {

			_smp::message_pool::getInstance().push(new message("[GrandPrix::checkAllClearHole][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
		}
	});

#if defined(_WIN32)
	LeaveCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_unlock(&m_cs_sync_shot);
#endif

	return (count == m_players.size());
}

void GrandPrix::setClearHole(PlayerGameInfo* _pgi) {

	if (_pgi == nullptr) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::setClearHole][Error] PlayerGameInfo* _pgi is invalid(nullptr).", CL_FILE_LOG_AND_CONSOLE));

		return;
	}

#if defined(_WIN32)
	EnterCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_lock(&m_cs_sync_shot);
#endif

	// Set
	_pgi->finish_hole = 1u;

#if defined(_WIN32)
	LeaveCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_unlock(&m_cs_sync_shot);
#endif
}

void GrandPrix::clearAllClearHole() {

#if defined(_WIN32)
	EnterCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_lock(&m_cs_sync_shot);
#endif

	clear_all_clear_hole();

#if defined(_WIN32)
	LeaveCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_unlock(&m_cs_sync_shot);
#endif
}

bool GrandPrix::checkAllClearHoleAndClear() {
	
	uint32_t count = 0u;
	bool ret = false;

#if defined(_WIN32)
	EnterCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_lock(&m_cs_sync_shot);
#endif

	// Check
	std::for_each(m_players.begin(), m_players.end(), [&](auto& _el) {

		try {

			INIT_PLAYER_INFO("checkAllClearHoleAndClear", "tentou verificar se todos os player terminaram o hole no jogo", _el);

			if (pgi->finish_hole)
				count++;

		}catch (exception& e) {

			_smp::message_pool::getInstance().push(new message("[GrandPrix::checkAllClearHoleAndClear][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
		}
	});

	ret = (count == m_players.size());

	// Clear
	if (ret)
		clear_all_clear_hole();

#if defined(_WIN32)
	LeaveCriticalSection(&m_cs_sync_shot);
#elif defined(__linux__)
	pthread_mutex_unlock(&m_cs_sync_shot);
#endif

	return ret;
}

void GrandPrix::clear_all_clear_hole() {

	std::for_each(m_players.begin(), m_players.end(), [&](auto& _el) {

		try {
			
			INIT_PLAYER_INFO("clear_all_clear_hole", " tentou limpar all clear hole no jogo", _el);

			pgi->finish_hole = 0u;

			// Also zeroes the hole-hit flag since he finished the hole
			pgi->shot_sync.state_shot.display.stDisplay.acerto_hole = 0u;
			pgi->data.giveup = 0u;

		}catch (exception& e) {

			_smp::message_pool::getInstance().push(new message("[GrandPrix::clear_all_hole][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
		}
	});
}

void GrandPrix::clear_timers() {

	try {

		// Begin Timer Hole
		m_timer_manager.lock();

		for (auto el : m_timer_manager.getTimers()) {

			if (el.m_timer != nullptr)
				sgs::gs::getInstance().unMakeTime(el.m_timer);
		}
	
		m_timer_manager.unlock();
		// End Time Hole

		// Begin Timer Rule
		m_timer_manager_rule.lock();

		for (auto el : m_timer_manager_rule.getTimers()) {

			if (el.m_timer != nullptr)
				sgs::gs::getInstance().unMakeTime(el.m_timer);
		}

		m_timer_manager_rule.unlock();
		// End Time Rule

		// Clears the timers
		m_timer_manager.clear();
		m_timer_manager_rule.clear();

	}catch (exception& e) {

		m_timer_manager.unlock();
		m_timer_manager_rule.unlock();

		_smp::message_pool::getInstance().push(new message("[GrandPrix::clear_timers][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::requestCalculeShotSpinningCube(player& _session, ShotSyncData& _ssd) {
	CHECK_SESSION_BEGIN("requestCalculeShotSpinningCube");

	try {

		// Only calculates if it is not short game and not grand prix rookie
		if (!m_ri.natural.stBit.short_game && !(sIff::getInstance().getGrandPrixAba(m_gp._typeid) == IFF::GrandPrixData::GP_ABA::ROOKIE && sIff::getInstance().isGrandPrixNormal(m_gp._typeid)))
			calcule_shot_to_spinning_cube(_session, _ssd);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestCalculeShotSpinningCube][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

void GrandPrix::requestCalculeShotCoin(player& _session, ShotSyncData& _ssd) {
	CHECK_SESSION_BEGIN("requestCalculeShotCoin");

	try {

		// Only calculates if it is not short game and not grand prix rookie
		if (!m_ri.natural.stBit.short_game && !(sIff::getInstance().getGrandPrixAba(m_gp._typeid) == IFF::GrandPrixData::GP_ABA::ROOKIE && sIff::getInstance().isGrandPrixNormal(m_gp._typeid)))
			calcule_shot_to_coin(_session, _ssd);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::requestCalculeShotCoin][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

bool GrandPrix::finish_game(player& _session, int option) {
	
	if (
#if defined(_WIN32)
		_session.m_sock != INVALID_SOCKET 
#elif defined(__linux__)
		_session.m_sock.fd != INVALID_SOCKET 
#endif
	&& _session.getState() && _session.isConnected() && m_players.size() > 0) {

		packet p;

		if (option == 6/*packet06 packet that ends the game*/) {

			if (m_grand_prix_state)
				finish_grand_prix(_session, 1);	// Ends without having finished playing

			INIT_PLAYER_INFO("finish_game", "tentou terminar o jogo", &_session);

			// Rookie Grand Prix does not change the player's info, only achievements
			if (!(sIff::getInstance().getGrandPrixAba(m_gp._typeid) == IFF::GrandPrixData::GP_ABA::ROOKIE && sIff::getInstance().isGrandPrixNormal(m_gp._typeid))) {
				
				// Save the record if the camp ended and the player did not finish all holes, the record must also be saved [OK][Done]
				requestSaveRecordCourse(_session, 52/*Grand Prix*/, (m_ri.qntd_hole == 18 && (m_course->findHoleSeq(pgi->hole) == 18 || pgi->flag == PlayerGameInfo::eFLAG_GAME::END_GAME)) ? 1 : 0);

				requestSaveInfo(_session, 0);
			}

			// Gives Exp to the Caddie AND Mascot too
			if (pgi->data.exp > 0) {	// only add exp if greater than 0

				// Add Exp to the player
				_session.addExp(pgi->data.exp, false/*Does not need the packet to change level*/);

				// Gives Exp to the Equipped Caddie
				if (_session.m_pi.ei.cad_info != nullptr)	// Has an equipped caddie
					_session.addCaddieExp(pgi->data.exp);

				// Gives Exp to the Equipped Mascot
				if (_session.m_pi.ei.mascot_info != nullptr)
					_session.addMascotExp(pgi->data.exp);
			}

			// Update Info Map Statistics
			sendUpdateInfoAndMapStatistics(_session, 0);

			// Update Mascot Info ON GAME, if the player has a mascot equipped
			if (_session.m_pi.ei.mascot_info != nullptr) {
				packet_func::pacote06B(p, &_session, &_session.m_pi, 8);

				packet_func::session_send(p, &_session, 1);
			}

			// Achievement Here
			pgi->sys_achieve.finish_and_update(_session);

			// Response sent every time a game ends, I do not know what it is yet, only the HIO Event does not have this one
			p.init_plain((unsigned short)0x244);

			p.addUint32(0);	// OK

			packet_func::session_send(p, &_session, 1);

			// This one is new from JP, it exists in Tourney, VS, Grand Prix, HIO Event, I did not check whether the others have it too
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

		}
	}

	return (PlayersCompleteGameAndClear() && m_grand_prix_state);
}

int GrandPrix::end_time(void* _arg1, void* _arg2) {
	
	auto game = reinterpret_cast< GrandPrix* >(_arg1);

	try {
		
		// Hole time is up
		game->timeIsOver(_arg2);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::end_time][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return 0;
}

int GrandPrix::end_time_rule(void* _arg1, void* _arg2) {

	auto game = reinterpret_cast< GrandPrix* >(_arg1);

	try {

		// Rule time is up
		game->timeRuleIsOver(_arg2);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::end_time_rule][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return 0;
}

void GrandPrix::startTimeRule(void* _quem) {

	try {
		
		if (_quem != nullptr && m_gp.rule > 0 && (m_gp.rule == eRULE::TIME_10_SEC || m_gp.rule == eRULE::TIME_15_SEC)) {

			DWORD time_milli = (m_gp.rule == eRULE::TIME_10_SEC ? 10u : (m_gp.rule == eRULE::TIME_15_SEC ? 15u : 0u));

			player *p = reinterpret_cast< player* >(_quem);

			// Stops the timer if there is already one
			auto timer = m_timer_manager_rule.findTimer(p);

			// There is no timer created yet, creates one for it
			if (timer == nullptr || timer->m_timer == nullptr) {

				if (timer == nullptr && (timer = m_timer_manager_rule.insertTimer(p, nullptr)) == nullptr)
					throw exception("[GrandPrix::startTimeRule][Error] Player[UID=" + std::to_string(p->m_pi.uid) 
							+ "] nao conseguiu criar um timer_ctx para poder criar um timer rule para o player. Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::GRAND_PRIX, 1050, 0));

				// Job
				job j(GrandPrix::end_time_rule, this, _quem);

				// Creates the rule timer
				timer->m_timer = sgs::gs::getInstance().makeTime(time_milli * 1000/*milliseconds*/, j);	// already in minutes milliseconds

#ifdef _DEBUG
				_smp::message_pool::getInstance().push(new message("[GrandPrix::startTimeRule][Log] Criou o Timer Rule[Tempo=" + std::to_string(time_milli) + "seg"
						+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] para o Player[UID=" + std::to_string(p->m_pi.uid) + "].", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
			
			}else {

				// There is already a timer, resets it and starts again
				if (timer->m_timer != nullptr) {
					
					if (timer->m_timer->getState() != timer::TIMER_STATE::STOPPED)
						timer->m_timer->stop();

					// starts it again
					timer->m_timer->start();

#ifdef _DEBUG
					_smp::message_pool::getInstance().push(new message("[GrandPrix::startTimeRule][Log] Reiniciou o Timer Rule[Tempo=" + std::to_string(time_milli) + "seg"
							+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] para o Player[UID=" + std::to_string(p->m_pi.uid) + "].", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
				}
			}

		}

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::startTimeRule][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}

bool GrandPrix::stopTimeRule(void* _quem) {
	
	
	bool ret = true;

	try {

		if (_quem != nullptr && m_gp.rule > 0 && (m_gp.rule == eRULE::TIME_10_SEC || m_gp.rule == eRULE::TIME_15_SEC)) {

			player *p = reinterpret_cast< player* >(_quem);

			auto timer = m_timer_manager_rule.findTimer(p);

			if (timer != nullptr && timer->m_timer != nullptr && timer->m_timer->getState() != timer::TIMER_STATE::STOPPED) {
				
				timer->m_timer->stop();

				DWORD time_milli = (m_gp.rule == eRULE::TIME_10_SEC ? 10u : (m_gp.rule == eRULE::TIME_15_SEC ? 15u : 0u));

#ifdef _DEBUG
				_smp::message_pool::getInstance().push(new message("[GrandPrix::stopTimeRule][Log] Parou o Timer Rule[Tempo=" + std::to_string(time_milli) + "seg"
						+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] para o Player[UID=" + std::to_string(p->m_pi.uid) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
			}
		}

	}catch (exception& e) {

		ret = false;

		_smp::message_pool::getInstance().push(new message("[GrandPrix::stopTimeRule][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return ret;
}

void GrandPrix::timeRuleIsOver(void* _quem) {

	try {

		if (_quem != nullptr && m_gp.rule > 0 && (m_gp.rule == eRULE::TIME_10_SEC || m_gp.rule == eRULE::TIME_15_SEC)) {

			player *s = reinterpret_cast< player* >(_quem);

			try {

				m_lock_manager.lock(s);

				auto timer = m_timer_manager_rule.findTimer(s);

				if (timer != nullptr && timer->m_timer != nullptr) {

					// Stops the timer if it is not stopped
					if (timer->m_timer->getState() != timer::TIMER_STATE::STOPPED)
						timer->m_timer->stop();

					// Updates the player's data: the Rule time ran out
					INIT_PLAYER_INFO("timeRuleIsOver", "acabou o tempo do hole do player", s);

					// Penalty because he did not shoot before the Rule time ran out
					if (m_game_init_state == 1 && pgi->init_shot == 0u)
						pgi->data.penalidade++;

					DWORD time_milli = (m_gp.rule == eRULE::TIME_10_SEC ? 10u : (m_gp.rule == eRULE::TIME_15_SEC ? 15u : 0u));

#ifdef _DEBUG
					_smp::message_pool::getInstance().push(new message("[GrandPrix::timeRuleIsOver][Log] Acabou o tempo do Timer Rule[Tempo=" + std::to_string(time_milli) + "seg"
							+ ", STATE=" + std::to_string(timer->m_timer->getState()) + "] do Player[UID=" + std::to_string(s->m_pi.uid) + "]", CL_FILE_LOG_AND_CONSOLE));
#endif // _DEBUG
				}

				m_lock_manager.unlock(s);
			
			}catch (exception& e) {
				UNREFERENCED_PARAMETER(e);

				m_lock_manager.unlock(s);

				// Rethrows to the outer try..catch to send the msg to the log
				throw;
			}

		}

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[GrandPrix::timeRuleIsOver][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}
}
