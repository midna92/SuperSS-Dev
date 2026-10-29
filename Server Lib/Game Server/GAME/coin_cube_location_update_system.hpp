// Arquivo coin_cube_location_update_system.hpp
// Created on 16/10/2020 at 20:35 by Acrisio
// Definition of the CoinCubeLocationUpdateSystem class

#pragma once
#ifndef _STDA_COIN_CUBE_LOCATION_UPDATE_SYSTEM_HPP
#define _STDA_COIN_CUBE_LOCATION_UPDATE_SYSTEM_HPP

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#include <pthread.h>
#include <unistd.h>
#endif

#include "../../Projeto IOCP/TYPE/singleton.h"
#include "../../Projeto IOCP/TYPE/list_async.h"
#include "../TYPE/coin_cube_type.hpp"

#include "../../Projeto IOCP/THREAD POOL/thread.h"

#include "../TYPE/hole_type.hpp"

#include <map>
#include <vector>
#include <ctime>

namespace stdA {

	typedef std::map< unsigned char/*course_id*/, std::map< unsigned char/*hole_number*/, std::vector< CubeEx > > > MAP_COURSE_COIN_CUBE;

	constexpr time_t UPDATE_TIME_INTERVALE_HOUR = 24u;

	class CoinCubeLocationUpdateSystem {

		public:
			CoinCubeLocationUpdateSystem();
			virtual ~CoinCubeLocationUpdateSystem();

			void load();

			bool isLoad();

			void pushOrderToCalcule(CalculeCoinCubeUpdateOrder _cccuo);

#ifdef _DEBUG
			void forceUpdate(); // Force the coin and cube location update ahead of time
#endif // _DEBUG

		protected:
			void initialize();

			void clear();

			void calculeShotCube(CalculeCoinCubeUpdateOrder& _cccuo/*PlayerInfo& _pi, PlayerGameInfo& _pgi, Location _last_position, Location& _pin, uint32_t _wind*/);
			void calculeShotCoin(CalculeCoinCubeUpdateOrder& _cccuo);

			void checkAndAddCoinCube(unsigned char _course_id, unsigned char _hole_number, CubeEx _cube);

			void update_spwan_location();

			uint32_t getLimitCoinCubePerParHole(unsigned char _par_hole);

		protected:
#if defined(_WIN32)
			static DWORD WINAPI _translateOrder(LPVOID lpParameter);
#elif defined(__linux__)
			static void* _translateOrder(LPVOID lpParameter);
#endif

#if defined(_WIN32)
			DWORD translateOrder();
#elif defined(__linux__)
			void* translateOrder();
#endif

		private:
			list_async< CalculeCoinCubeUpdateOrder > m_pedidos;

			bool m_load;

			thread* m_thread;

			uint32_t volatile m_continue_translate;

			time_t m_update_location_time;						// Update the coin and cube spawns that were generated in the determined time

			MAP_COURSE_COIN_CUBE m_course_coin_cube;			// This is what it is generating from the players' shots
			MAP_COURSE_COIN_CUBE m_course_coin_cube_current;	// This is the current one that it is spawning in the course

#if defined(_WIN32)
			CRITICAL_SECTION m_cs;
#elif defined(__linux__)
			pthread_mutex_t m_cs;
#endif

	};

	typedef Singleton< CoinCubeLocationUpdateSystem > sCoinCubeLocationUpdateSystem;
}

#endif // !_STDA_COIN_CUBE_LOCATION_UPDATE_SYSTEM_HPP
