// File room_grand_zodiac_event.hpp
// Created on 26/06/2020 at 16:33 by Acrisio
// Definition of the RoomGrandZodiacEvent class

#pragma once
#ifndef _STDA_ROOM_GRAND_ZODIAC_EVENT_HPP
#define _STDA_ROOM_GRAND_ZODIAC_EVENT_HPP

#if defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>

#include "../../Projeto IOCP/UTIL/event.hpp"
#endif

#include "room.h"

#include "../../Projeto IOCP/THREAD POOL/thread.h"
#include "../../Projeto IOCP/TIMER/timer.h"

namespace stdA {
	class RoomGrandZodiacEvent : public room {
		public:
			enum eSTATE_ROOM_GRAND_ZODIAC_EVENT_SYNC : unsigned char {
				WAIT_TIME_START,
				WAIT_10_SECONDS_START,
				WAIT_END_GAME,
			};

			struct stStateRoomGrandZodiacEventSync {
				stStateRoomGrandZodiacEventSync() : m_state(eSTATE_ROOM_GRAND_ZODIAC_EVENT_SYNC::WAIT_TIME_START) {

#if defined(_WIN32)
					InitializeCriticalSection(&m_cs);
#elif defined(__linux__)
					INIT_PTHREAD_MUTEXATTR_RECURSIVE;
					INIT_PTHREAD_MUTEX_RECURSIVE(&m_cs);
					DESTROY_PTHREAD_MUTEXATTR_RECURSIVE;
#endif
				};

				~stStateRoomGrandZodiacEventSync() {

					m_state = eSTATE_ROOM_GRAND_ZODIAC_EVENT_SYNC::WAIT_TIME_START;

#if defined(_WIN32)
					DeleteCriticalSection(&m_cs);
#elif defined(__linux__)
					pthread_mutex_destroy(&m_cs);
#endif
				};

				void lock() {
#if defined(_WIN32)
					EnterCriticalSection(&m_cs);
#elif defined(__linux__)
					pthread_mutex_lock(&m_cs);
#endif
				};

				void unlock() {
#if defined(_WIN32)
					LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
					pthread_mutex_unlock(&m_cs);
#endif
				};

				eSTATE_ROOM_GRAND_ZODIAC_EVENT_SYNC& getState() {
					return m_state;
				};

				void setState(eSTATE_ROOM_GRAND_ZODIAC_EVENT_SYNC _state) {

					m_state = _state;
				};

				void setStateWithLock(eSTATE_ROOM_GRAND_ZODIAC_EVENT_SYNC _state) {

					lock();

					m_state = _state;

					unlock();
				}

			protected:

				eSTATE_ROOM_GRAND_ZODIAC_EVENT_SYNC m_state;

#if defined(_WIN32)
				CRITICAL_SECTION m_cs;
#elif defined(__linux__)
				pthread_mutex_t m_cs;
#endif
			};

			// Static Instance vector struct
			struct RoomGrandZodiacEventInstanciaCtx {
			public:
				enum eSTATE : unsigned char {
					GOOD,
					DESTROYING,
					DESTROYED,
				};

			public:
				RoomGrandZodiacEventInstanciaCtx(RoomGrandZodiacEvent* _rgze, eSTATE _state)
					: m_rgze(_rgze), m_state(_state) {
				};

			public:
				RoomGrandZodiacEvent* m_rgze;
				eSTATE m_state;
			};

			struct CriticalSectionInstancia {
				CriticalSectionInstancia() : m_state(false), m_lock(false) {

					init();

				};
				~CriticalSectionInstancia() {

					if (m_state)
#if defined(_WIN32)
						DeleteCriticalSection(&m_cs);
#elif defined(__linux__)
						pthread_mutex_destroy(&m_cs);
#endif
				};
				void init() {

					if (!m_state) {
#if defined(_WIN32)
						InitializeCriticalSection(&m_cs);
#elif defined(__linux__)
						INIT_PTHREAD_MUTEXATTR_RECURSIVE;
						INIT_PTHREAD_MUTEX_RECURSIVE(&m_cs);
						DESTROY_PTHREAD_MUTEXATTR_RECURSIVE;
#endif
					}

					m_state = true;
				};
				void lock() {

					if (!m_state)
						init();

#if defined(_WIN32)
					EnterCriticalSection(&m_cs);
#elif defined(__linux__)
					pthread_mutex_lock(&m_cs);
#endif

					// Is locked
					m_lock = true;

				};
				void unlock() {

					if (!m_lock)
						return;	// Is not locked

					// Unlock
					m_lock = false;

#if defined(_WIN32)
					LeaveCriticalSection(&m_cs);
#elif defined(__linux__)
					pthread_mutex_unlock(&m_cs);
#endif
				};
#if defined(_WIN32)
				CRITICAL_SECTION m_cs;
#elif defined(__linux__)
				pthread_mutex_t m_cs;
#endif
				bool m_state;
				bool m_lock;
			};

		public:
			RoomGrandZodiacEvent(unsigned char _channel_owner, RoomInfoEx _ri);
			virtual ~RoomGrandZodiacEvent();

			virtual eROOM_CLASS_TYPE getClassType() override;

			virtual bool isAllReady() override;

			// The time-based Grand Prix has its own startGame since the server is the one that starts it
			bool startGame();

			// Init Instance vector and lock, to avoid an error in the destructor since it will be destroyed before the class instance
			static void initFirstInstance();

		protected:
#if defined(_WIN32)
			static DWORD WINAPI _waitTimeStart(LPVOID lpParameter);
#elif defined(__linux__)
			static void* _waitTimeStart(LPVOID lpParameter);
#endif
			static int _count_down_time(void* _arg1, void* _arg2);

#if defined(_WIN32)
			DWORD waitTimeStart();
#elif defined(__linux__)
			void* waitTimeStart();
#endif
			void count_down(int64_t _sec_to_start);

			// Finish Thread Sync wait time start
			void finish_thread_sync_wait_time_start();

			void clear_timer_count_down();

		protected:
			stStateRoomGrandZodiacEventSync m_state_rgze;

			SYSTEMTIME m_create_room;			// Date the room was created

			timer *m_timer_count_down;

			thread *m_wait_time_start;

#if defined(_WIN32)
			HANDLE m_hEvent_wait_start;			// Event to finish the sync wait time start thread
			HANDLE m_hEvent_wait_start_pulse;	// Event to pulse the sync wait time start thread to go faster when a player enters the room
#elif defined(__linux__)
			Event *m_hEvent_wait_start;			// Event to finish the sync wait time start thread
			Event *m_hEvent_wait_start_pulse;	// Event to pulse the sync wait time start thread to go faster when a player enters the room
#endif

		private:
			// Static functions and variables to ensure safe handling of the Grand Zodiac Event room pointer that uses the timer with a callback function
			static void push_instancia(RoomGrandZodiacEvent* _rgze);
			static void pop_instancia(RoomGrandZodiacEvent* _rgze);

			static void set_instancia_state(RoomGrandZodiacEvent* _rgze, RoomGrandZodiacEventInstanciaCtx::eSTATE _state);

			static int get_instancia_index(RoomGrandZodiacEvent* _rgze);

			static bool instancia_valid(RoomGrandZodiacEvent* _rgze);

			typedef Singleton< CriticalSectionInstancia > m_cs_instancia;
			typedef Singleton< std::vector< RoomGrandZodiacEventInstanciaCtx > > m_instancias;
	};
}

#endif // !_STDA_ROOM_GRAND_ZODIAC_EVENT_HPP
