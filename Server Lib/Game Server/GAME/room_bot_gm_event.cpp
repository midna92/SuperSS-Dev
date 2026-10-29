// File room_bot_gm_event.cpp
// Created on 02/11/2020 at 01:37 by Acrisio
// Implementation of the RoomBotGMEvent class

#if defined(_WIN32)
#pragma pack(1)
#endif

#if defined(_WIN32)
#include <WinSock2.h>
#endif

#include "room_bot_gm_event.hpp"

#include "../PACKET/packet_func_sv.h"

#include "../../Projeto IOCP/THREAD POOL/threadpool_base.hpp"

#include "../../Projeto IOCP/UTIL/util_time.h"

#include "../Game Server/game_server.h"

#include "tourney.hpp"

#include "../UTIL/lottery.hpp"

#include "mail_box_manager.hpp"
#include "item_manager.h"

#if defined(_WIN32)
#include <DbgHelp.h>
#endif

using namespace stdA;

#define CHECK_SESSION_BEGIN(method) if (!_session.getState()) \
										throw exception("[RoomBotGMEvent::" + std::string((method)) + "][Error] player nao esta connectado", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 12, 0)); \

#define REQUEST_BEGIN(method) CHECK_SESSION_BEGIN(std::string("request") + (method)) \
							  if (_packet == nullptr) \
									throw exception("[RoomBotGMEvent::request" + std::string((method)) + "][Error] _packet is nullptr", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 12, 0)); \

RoomBotGMEvent::RoomBotGMEvent(unsigned char _channel_owner, RoomInfoEx _ri, std::vector< stReward > _rewards) : room(_channel_owner, _ri), m_state_rbge(), m_create_room{ 0 },
	m_rewards(_rewards), m_wait_time_start(nullptr), 
#if defined(_WIN32)
	m_hEvent_wait_start(INVALID_HANDLE_VALUE), m_hEvent_wait_start_pulse(INVALID_HANDLE_VALUE),
#elif defined(__linux__)
	m_hEvent_wait_start(nullptr), m_hEvent_wait_start_pulse(nullptr),
#endif
	m_timer_count_down(nullptr) {

	// Puts the instance of the class that was just created in the static vector
	push_instancia(this);

	// Date the room was created
	GetLocalTime(&m_create_room);

	// Creates event that goes to the wait time start thread
#if defined(_WIN32)
	if ((m_hEvent_wait_start = CreateEvent(NULL, TRUE, FALSE, NULL)) == INVALID_HANDLE_VALUE)
		throw exception("[RoomBotGMEvent::RoomBotGMEvent][Error] ao criar evento wait time start.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 1050, GetLastError()));
#elif defined(__linux__)

	m_hEvent_wait_start = new Event(true, 0u);

	if (!m_hEvent_wait_start->is_good()) {

		delete m_hEvent_wait_start;

		m_hEvent_wait_start = nullptr;

		throw exception("[RoomBotGMEvent::RoomBotGMEvent][Error] ao criar evento wait time start.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 1050, errno));
	}
#endif

	// Creates event that will pulse the wait time start thread to go faster when a player enters or leaves the room
#if defined(_WIN32)
	if ((m_hEvent_wait_start_pulse = CreateEvent(NULL, FALSE, FALSE, NULL)) == INVALID_HANDLE_VALUE)
		throw exception("[RoomBotGMEvent::RoomBotGMEvent][Error] ao criar evento wait time start pulse.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 1050, GetLastError()));
#elif defined(__linux__)

	m_hEvent_wait_start_pulse = new Event(false, 0u);

	if (!m_hEvent_wait_start_pulse->is_good()) {

		delete m_hEvent_wait_start_pulse;

		m_hEvent_wait_start_pulse = nullptr;

		throw exception("[RoomBotGMEvent::RoomBotGMEvent][Error] ao criar evento wait time start pulse.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 1050, errno));
	}
#endif

	// Creates the thread that will synchronize the time to start the Bot GM Event
	m_wait_time_start = new thread(1063/*Wait Time Start*/, RoomBotGMEvent::_waitTimeStart, (LPVOID)this);

	// Puts the trophy
	m_ri.flag_gm = 1;

	m_ri.state_flag = 0x100;

	m_ri.trofel = TROFEL_GM_EVENT_TYPEID;
}

RoomBotGMEvent::~RoomBotGMEvent() {

	if (!m_rewards.empty()) {
		m_rewards.clear();
		m_rewards.shrink_to_fit();
	}

	clear_timer_count_down();

	// Finish Thread Sync wait time start
	finish_thread_sync_wait_time_start();

	// Removes the class instance from the static vector, because the room will be destroyed
	pop_instancia(this);
}

eROOM_CLASS_TYPE RoomBotGMEvent::getClassType() {
	return eROOM_CLASS_TYPE::RCT_GRAND_ZODIAC_EVENT;
}

bool RoomBotGMEvent::isAllReady() {
	
	// is always true because whoever starts the game in this room is always the server
	// The client errors out when starting if there is a guest in the room
	// so check that there is no guest in the room
	return !_haveInvited();
}

bool RoomBotGMEvent::startGame() {
	
	packet p;

	bool ret = true;

	try {

		// Checks if a game is already initialized and throws an error if so, so the client receives a response
		if (m_pGame != nullptr)
			throw exception("[RoomBotGMEvent::startGame][Error] Server tentou comecar o jogo na sala[NUMERO=" 
					+ std::to_string(m_ri.numero) + "], mas ja tem um jogo inicializado. Hacker ou Bug", 
					STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 8, 0x5900202));

		// Checks if everyone is ready, otherwise errors
		if (!isAllReady())
			throw exception("[RoomBotGMEvent::startGame][Error] Server tentou comecar o jogo na sala[NUMERO=" 
					+ std::to_string(m_ri.numero) + ", MASTER=" + std::to_string(m_ri.master) 
					+ "], mas nem todos jogadores estao prontos. Hacker ou Bug.", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 8, 0x5900202));

		if (m_ri.course >= 0x7Fu) {

			// Special Shuffle Course
			if (m_ri.tipo == RoomInfo::TIPO::SPECIAL_SHUFFLE_COURSE && m_ri.modo == Hole::eMODO::M_SHUFFLE_COURSE) {

				m_ri.course = RoomInfo::eCOURSE(0x80 | RoomInfo::eCOURSE::CHRONICLE_1_CHAOS/*SPECIAL SHUFFLE COURSE*/);

			}else {	// Normal random

				Lottery lottery((uint64_t)this);

				for (auto& el : sIff::getInstance().getCourse()) {
				
					auto course_id = sIff::getInstance().getItemIdentify(el.second._typeid);

					if (course_id != 17/*SSC*/ && course_id != 0x40/*GRAND ZODIAC*/)
						lottery.push(100, course_id);
				}

				auto lc = lottery.spinRoleta();

				if (lc != nullptr)
					m_ri.course = RoomInfo::eCOURSE(0x80u | (unsigned char)lc->value);
			}
		}

		RateValue rv{ 0 };

		rv.exp = m_ri.rate_exp = sgs::gs::getInstance().getInfo().rate.exp;
		rv.pang = m_ri.rate_pang = sgs::gs::getInstance().getInfo().rate.pang;

		// Angel Event
		m_ri.angel_event = sgs::gs::getInstance().getInfo().rate.angel_event;

		rv.clubset = sgs::gs::getInstance().getInfo().rate.club_mastery;
		rv.rain = sgs::gs::getInstance().getInfo().rate.chuva;
		rv.treasure = sgs::gs::getInstance().getInfo().rate.treasure;

		rv.persist_rain = 0u;	// Persist rain flag, this is done in the game class

		switch (m_ri.tipo) {
		case RoomInfo::TIPO::TOURNEY:
			m_pGame = new Tourney(v_sessions, m_ri, rv, m_ri.channel_rookie);
			break;
		default:
			throw exception("[RoomBotGMEvent::startGame][Error] Server tentou comecar o jogo na sala[NUMERO=" 
					+ std::to_string(m_ri.numero) + ", MASTER=" + std::to_string(m_ri.master)
					+ "], mas o tipo da sala nao eh Tourney. Hacker ou Bug", STDA_MAKE_ERROR(STDA_ERROR_TYPE::ROOM_BOT_GM_EVENT, 9, 0x5900202));
		}

		// Checks if there is only 1 player in the room, if so creates a Bot so the player can play
		if (v_sessions.size() == 1u)
			addBotVisual(*v_sessions.front());

		// Update Room State
		m_ri.state = 0;	// IN GAME

		p.init_plain((unsigned short)0x230);

		packet_func::room_broadcast(*this, p, 1);

		p.init_plain((unsigned short)0x231);

		packet_func::room_broadcast(*this, p, 1);

		uint32_t rate_pang = sgs::gs::getInstance().getInfo().rate.pang;

		p.init_plain((unsigned short)0x77);

		p.addUint32(rate_pang);	// Rate Pang

		packet_func::room_broadcast(*this, p, 1);

		// Makes the thread that creates the timer wait for the game to end
		m_state_rbge.setStateWithLock(eSTATE_ROOM_BOT_GM_EVENT_SYNC::WAIT_END_GAME);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::startGame][Error] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));

		ret = false;	// Error initializing the Game
	}

	return ret;
}

void RoomBotGMEvent::initFirstInstance() {

	if (m_cs_instancia::getInstance().m_state && m_instancias::getInstance().empty())
		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::initFirstInstance][Log] Criou primeira instance do Singleton da classe Room Bot GM Event static vector.", CL_FILE_LOG_AND_CONSOLE));
}

#if defined(_WIN32)
DWORD WINAPI RoomBotGMEvent::_waitTimeStart(LPVOID lpParameter) {
#elif defined(__linux__)
void* RoomBotGMEvent::_waitTimeStart(LPVOID lpParameter) {
#endif
	BEGIN_THREAD_SETUP(RoomBotGMEvent);

	result = pTP->waitTimeStart();

	END_THREAD_SETUP("waitTimeStart()");
}

#if defined(_WIN32)
DWORD RoomBotGMEvent::waitTimeStart() {
#elif defined(__linux__)
void* RoomBotGMEvent::waitTimeStart() {
#endif
	
	try {

		// Log
		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::waitTimeStart][Log] waitTimeStart iniciado com sucesso!", CL_FILE_LOG_AND_CONSOLE));

		packet p;

		DWORD retWait = WAIT_TIMEOUT;

#if defined(_WIN32)
		HANDLE wait_events[2] = { m_hEvent_wait_start, m_hEvent_wait_start_pulse };

		while ((retWait = WaitForMultipleObjects((sizeof(wait_events) / sizeof(wait_events[0])), wait_events, false, 1000/*1 second*/)) == WAIT_TIMEOUT || retWait == (WAIT_OBJECT_0 + 1)) {
#elif defined(__linux__)
		std::vector< Event* > wait_events = { m_hEvent_wait_start, m_hEvent_wait_start_pulse };

		while ((retWait = Event::waitMultipleEvent(wait_events.size(), wait_events, false, 1000/*1 second*/)) == WAIT_TIMEOUT || retWait == (WAIT_OBJECT_0 + 1)) {
#endif

			try {

				m_state_rbge.lock();

				switch (m_state_rbge.getState()) {
					case eSTATE_ROOM_BOT_GM_EVENT_SYNC::WAIT_TIME_START:
					{

						if (m_timer_count_down == nullptr) {

							// It tries to start, if there is nobody in the room it destroys it
							if ((getLocalTimeDiff(m_create_room) / STDA_10_MICRO_PER_MIN) >= 2 /*&& v_sessions.size() > 0*/) {

								count_down(10); // 10 seconds

								m_state_rbge.setState(eSTATE_ROOM_BOT_GM_EVENT_SYNC::WAIT_10_SECONDS_START);

							}else if (_getRealNumPlayersWithoutInvited() == m_ri.max_player) {

								// The room reached the maximum number of players
								// Starts the Bot GM Event in 10 seconds
								packet p((unsigned short)0x40);

								p.addUint8(12);	// Msg that the room reached the maximum players

								p.addUint16(0u);	// Nickname empty
								p.addUint16(0u);	// Msg Empty

								p.addUint32(10u);	// 10 seconds to start the Bot GM Event

								packet_func::room_broadcast(*this, p, 1);

								count_down(10); // 10 seconds

								m_state_rbge.setState(eSTATE_ROOM_BOT_GM_EVENT_SYNC::WAIT_10_SECONDS_START);
							}
						}

						break;
					}
					case eSTATE_ROOM_BOT_GM_EVENT_SYNC::WAIT_10_SECONDS_START:
					{
						// Here it lets the time run out and closes the room if nobody is in it
						// Does nothing
						break;
					}
					case eSTATE_ROOM_BOT_GM_EVENT_SYNC::WAIT_END_GAME:
					{
						// Does nothing
						break;
					}
				}

				// Release
				m_state_rbge.unlock();

			}catch (exception& e) {

				// Release
				m_state_rbge.unlock();

				_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::waitTimeStart][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
			}
		}
		
	}catch (exception& e) {
		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::waitTimeStart][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}catch (std::exception& e) {
		_smp::message_pool::getInstance().push(new message(std::string("[RoomBotGMEvent::waitTimeStart][ErrorSystem] ") + e.what(), CL_FILE_LOG_AND_CONSOLE));
	}catch (...) {
		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::waitTimeStart][ErrorSystem] waitTimeStart() -> Exception (...) c++ nao tratada ou uma excessao de C(nullptr e etc)\n", CL_FILE_LOG_AND_CONSOLE));
	}

#ifdef _DEBUG
	_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::waitTimeStart][Log] Saindo de waitTimeStart()...", CL_FILE_LOG_AND_CONSOLE));
#else
	_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::waitTimeStart][Log] Saindo de waitTimeStart()...", CL_ONLY_FILE_LOG));
#endif // _DEBUG

#if defined(_WIN32)
	return 0;
#elif defined(__linux__)
	return (void*)0;
#endif
}

int RoomBotGMEvent::_count_down_time(void* _arg1, void* _arg2) {
	
	RoomBotGMEvent *rbge = reinterpret_cast< RoomBotGMEvent* >(_arg1);
	int64_t sec_to_start = reinterpret_cast< int64_t >(_arg2);

	try {

		if (rbge != nullptr && instancia_valid(rbge))
			if (rbge->count_down(sec_to_start))
				sgs::gs::getInstance().destroyRoom(rbge->m_channel_owner, rbge->m_ri.numero); // Destroys the room, if there are no players, or it could not initialize

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::_count_down_time][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return 0;
}

int RoomBotGMEvent::count_down(int64_t _sec_to_start) {

	int ret = 0;

	try {

		// Locks the room to avoid a conflict error
		lock();

		if (_sec_to_start <= 0) {	// Starts the game
			
			// deletes the timer if it still exists
			clear_timer_count_down();

			// Starts the game if there is at least 1 player in the room
			if (v_sessions.size() >= 1 && startGame())
				sgs::gs::getInstance().sendUpdateRoomInfo(this, 3); // Update Room Info
			else if (v_sessions.size() >= 1)
				// Sets it to wait for the game start time again, because it could not create the room
				// There may be a guest in the room, then the game cannot start because the client freezes
				count_down(10);
			else
				ret = 1; // Destroys the room

		}else {

			uint32_t wait = 0u;
			int32_t rest = 0;

			unsigned char type = 1u;

			DWORD interval = 0u;
			float diff = 0.f;

			int32_t elapsed_sec = (m_timer_count_down != nullptr) ? (int)std::round(m_timer_count_down->getElapsed() / 1000.f)/*Milli to seconds*/ : 0;

			_sec_to_start -= elapsed_sec;

			if ((diff = ((_sec_to_start - 10/*10 seconds*/) / 30.f/* 30 seconds*/)) >= 1.f) {	// Interval of 30 seconds

				if ((_sec_to_start % 30) == 0) {
					
					// Interval
					interval = 30 * 1000;	// 30 seconds

					wait = interval * (int)diff;	// 30 * diff minutes in milliseconds
				
				}else {

					// Corrects the time to stay in the right interval
					wait = interval = (_sec_to_start % 30) * 1000;
				
				}

			}else if ((diff = ((_sec_to_start - 1/*1 second*/) / 10.f/*10 seconds*/)) >= 1.f) {			// Interval of 10 seconds
			
				if ((_sec_to_start % 10) == 0) {
					
					// Interval
					interval = 10 * 1000;	// 10 seconds

					wait = interval * (int)diff;	// 10 * diff seconds in milliseconds
				
				}else {

					// Corrects the time to stay in the right interval
					wait = interval = (_sec_to_start % 10) * 1000;
				}

			}else {		// Interval of 1 second

				diff = std::round(_sec_to_start / 1.f);

				// Interval
				interval = 1000;	// 1 second
			
				wait = interval * (int)diff;	// 1 * diff seconds in milliseconds

			}

			// UPDATE ON GAME
			packet p((unsigned short)0x40);

			p.addUint8(11);	// Grand Prix and Grand Zodiac Timer

			p.addUint16(0u);	// Nick
			p.addUint16(0u);	// Msg

			p.addUint32((uint32_t)_sec_to_start);

			packet_func::room_broadcast(*this, p, 1);

			// Make Timer
			if (m_timer_count_down == nullptr || m_timer_count_down->getState() == timer::STOP ||
				m_timer_count_down->getState() == timer::STOPPING || m_timer_count_down->getState() == timer::STOPPED) {

				// Make Time
				job _job(RoomBotGMEvent::_count_down_time, this, (void*)_sec_to_start);

				// If the Shutdown Timer is created, delete it and create a new one
				if (m_timer_count_down != nullptr)
					clear_timer_count_down();

				m_timer_count_down = sgs::gs::getInstance().makeTime(wait, _job, std::vector< DWORD > { interval });
			}
		}

		// Release
		unlock();

	}catch (exception& e) {

		// Release
		unlock();

		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::count_down][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	return ret;
}

void RoomBotGMEvent::finish_thread_sync_wait_time_start() {

	try {
		
		if (m_wait_time_start != nullptr) {

#if defined(_WIN32)
			if (m_wait_time_start != INVALID_HANDLE_VALUE)
				SetEvent(m_hEvent_wait_start);
#elif defined(__linux__)
			if (m_wait_time_start != nullptr)
				m_hEvent_wait_start->set();
#endif

			m_wait_time_start->waitThreadFinish(INFINITE);

			delete m_wait_time_start;
		}

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::finish_thread_sync_wait_time_start][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));

		if (m_wait_time_start != nullptr) {

			m_wait_time_start->exit_thread();

			delete m_wait_time_start;
		}
	}

	m_wait_time_start = nullptr;

#if defined(_WIN32)
	if (m_hEvent_wait_start != INVALID_HANDLE_VALUE)
		CloseHandle(m_hEvent_wait_start);

	if (m_hEvent_wait_start_pulse != INVALID_HANDLE_VALUE)
		CloseHandle(m_hEvent_wait_start_pulse);

	m_hEvent_wait_start = INVALID_HANDLE_VALUE;
	m_hEvent_wait_start_pulse = INVALID_HANDLE_VALUE;
#elif defined(__linux__)
	if (m_hEvent_wait_start != nullptr)
		delete m_hEvent_wait_start;

	if (m_hEvent_wait_start_pulse != nullptr)
		delete m_hEvent_wait_start_pulse;

	m_hEvent_wait_start = nullptr;
	m_hEvent_wait_start_pulse = nullptr;
#endif
}

void RoomBotGMEvent::clear_timer_count_down() {

	try {

		if (m_timer_count_down != nullptr)
			sgs::gs::getInstance().unMakeTime(m_timer_count_down);

	}catch (exception& e) {

		_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::clear_timer_count_down][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
	}

	m_timer_count_down = nullptr;
}

void RoomBotGMEvent::finish_game() {

	if (m_pGame != nullptr) {

		// Sends gifts here, but there must be players in the game vector
		if (m_pGame->getSessions().empty() || m_rewards.empty()) {

			room::finish_game();

			return;
		}

		// Sends the gifts here

		// Lambda[getItemName]
		auto getItemName = [](uint32_t _typeid) -> std::string {

			std::string ret = "";

			auto base = sIff::getInstance().findCommomItem(_typeid);

			if (base != nullptr)
				ret = std::string(base->name);

			return ret;
		};

		std::string reward_str = "{";

		for (auto it_r = m_rewards.begin(); it_r != m_rewards.end(); ++it_r) {

			if (it_r != m_rewards.begin())
				reward_str += ", [";
			else
				reward_str += "[";

			reward_str += it_r->toString() + "]";
		}

		reward_str += "}";

		try {

			std::vector< stItem > v_item;
			stItem item{ 0 };
			BuyItem bi{ 0 };

			player* p = nullptr;

			for (auto& el_p : v_sessions) {

				// Clear, because it is per player
				v_item.clear();

				if (el_p == nullptr || (p = sgs::gs::getInstance().findPlayer(el_p->m_pi.uid)) == nullptr) {

					// Log, Player who won is no longer online, will be left without the item
					_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::finish_game][WARNING] Player[UID=" + (el_p == nullptr ? "UNKNOWN" : std::to_string(el_p->m_pi.uid))
							+ "] ganhou o item(ns)" + reward_str + ", mas saiu antes dos pr�mios ser entregues, vai ficar sem o pr�mio.", CL_FILE_LOG_AND_CONSOLE));

					continue;
				}

				// Initialize itens
				for (auto& el_r : m_rewards) {

					// Clear
					bi.clear();
					item.clear();

					// Initialize
					bi.id = -1;
					bi._typeid = el_r._typeid;
					bi.qntd = el_r.qntd;
					bi.time = (unsigned short)el_r.qntd_time;

					item_manager::initItemFromBuyItem(p->m_pi, item, bi, false, 0, 0, 1/*~does not Check Level*/);

					if (item._typeid == 0)
						_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::finish_game][Error][WARNING] tentou enviar o reward para o player[UID="
								+ std::to_string(p->m_pi.uid) + "] o Item[" + el_r.toString() + "], mas nao conseguiu inicializar o item. Bug", CL_FILE_LOG_AND_CONSOLE));

					v_item.push_back(item);
				}

				auto msg = std::string("Bot GM Event(" + _formatDate(m_create_room) + "): item[ " + (v_item.empty() ? "NONE" : getItemName(v_item.front()._typeid)) + " ]");

				if (v_item.empty() || MailBoxManager::sendMessageWithItem(0, p->m_pi.uid, msg, v_item) <= 0)
					_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::finish_game][Error][WARNING] tentou enviar reward para o player[UID="
							+ std::to_string(p->m_pi.uid) + "] o Item(ns)" + reward_str + ", mas nao conseguiu colocar o item no mail box dele. Bug", CL_FILE_LOG_AND_CONSOLE));

				// Log
				_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::finish_game][Log] Player[UID=" + std::to_string(p->m_pi.uid) 
						+ "] ganhou no Bot GM Event(" + _formatDate(m_create_room) + "): Item(" + std::to_string(m_rewards.size()) + ")" + reward_str, CL_FILE_LOG_AND_CONSOLE));
			}

		}catch (exception& e) {

			_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::finish_game][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
		}
		
		// Actually finishes the game
		room::finish_game();

		// 2min to destroy the room
		auto destroyRoomTimer = [](void* _arg1, void* _arg2) -> int {
			RoomBotGMEvent *rbge = reinterpret_cast< RoomBotGMEvent* >(_arg1);
			int64_t sec_to_start = reinterpret_cast< int64_t >(_arg2);

			try {

				if (rbge != nullptr && RoomBotGMEvent::instancia_valid(rbge))
					sgs::gs::getInstance().destroyRoom(rbge->m_channel_owner, rbge->m_ri.numero); // Destroys the room, if there are no players, or it could not initialize

			}catch (exception& e) {

				_smp::message_pool::getInstance().push(new message("[RoomBotGMEvent::lambda(destroyRoom)][ErrorSystem] " + e.getFullMessageError(), CL_FILE_LOG_AND_CONSOLE));
			}

			return 0;
		};

		// Make Timer
		job _job(destroyRoomTimer, this, (void*)0);

		// If the Shutdown Timer is created, delete it and create a new one
		if (m_timer_count_down != nullptr)
			clear_timer_count_down();

		m_timer_count_down = sgs::gs::getInstance().makeTime(2 * 60000, _job);
	}

}

bool RoomBotGMEvent::isDropRoom() {
	return false; // Do not drop (destroy) the room
}

void RoomBotGMEvent::push_instancia(RoomBotGMEvent* _rbge) {

	m_cs_instancia::getInstance().lock();

	m_instancias::getInstance().push_back(RoomBotGMEventInstanciaCtx(_rbge, RoomBotGMEventInstanciaCtx::eSTATE::GOOD));

	m_cs_instancia::getInstance().unlock();
}

void RoomBotGMEvent::pop_instancia(RoomBotGMEvent* _rbge) {

	m_cs_instancia::getInstance().lock();

	auto index = get_instancia_index(_rbge);

	if (index >= 0)
		m_instancias::getInstance().erase(m_instancias::getInstance().begin() + index);

	m_cs_instancia::getInstance().unlock();
}

void RoomBotGMEvent::set_instancia_state(RoomBotGMEvent* _rbge, RoomBotGMEventInstanciaCtx::eSTATE _state) {

	m_cs_instancia::getInstance().lock();

	auto index = get_instancia_index(_rbge);

	if (index >= 0)
		m_instancias::getInstance()[index].m_state = _state;

	m_cs_instancia::getInstance().unlock();
}

int RoomBotGMEvent::get_instancia_index(RoomBotGMEvent* _rbge) {
	
	int index = -1;

	for (auto i = 0u; i < m_instancias::getInstance().size(); ++i) {

		if (m_instancias::getInstance()[i].m_rbge == _rbge) {

			index = (int)i;

			break;
		}
	}

	return index;
}

bool RoomBotGMEvent::instancia_valid(RoomBotGMEvent* _rbge) {
	
	bool valid = false;

	m_cs_instancia::getInstance().lock();

	auto index = get_instancia_index(_rbge);

	if (index >= 0)
		valid = (m_instancias::getInstance()[index].m_state == RoomBotGMEventInstanciaCtx::eSTATE::GOOD);

	m_cs_instancia::getInstance().unlock();

	return valid;
}
