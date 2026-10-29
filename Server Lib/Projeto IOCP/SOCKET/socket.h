// File socket.h
// Created on 02/03/2017 by Acrisio
// Definition of class socket

#pragma once
#ifndef _STDA_SOCKET_H
#define _STDA_SOCKET_H

#if defined(_WIN32)
	#ifndef _WINSOCK2_H
		#define _WINSOCK2_H
		#include <winsock2.h>
	#endif
#elif defined(__linux__)
	#include "../UTIL/WinPort.h"
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <sys/types.h>
	#include <pthread.h>
	#include <time.h>
	#include "../THREAD POOL/thread.h"

	#define CLOCK_SOCKET_TO_CHECK CLOCK_MONOTONIC_RAW
#endif

#include <string>
#include <vector>

namespace stdA {
	class socket {
	public:
		explicit socket(int option = 0);

		explicit socket(SOCKET socket, int option = 0);

		~socket();

		SOCKET detatch();

		void attach(SOCKET socket);

		void close();

		void abortiveClose();

		void shutdown(int como);

		void listen(int max_listen_same_time);

		void bind(const SOCKADDR_IN &address);

		void connect(std::string host, size_t port);

		void connect(const SOCKADDR_IN* _s_addr);

	private:
#if defined(_WIN32)
#define DIFF_TICK(a, b, c) (INT64)(((INT64)((a.QuadPart) - (b.QuadPart)) * 1000000 / (c.QuadPart)) / 1000)
#endif

		typedef struct _connection_history {
			void clear() { memset(this, 0, sizeof(_connection_history)); };
			_connection_history* next;
			UINT32 ip;
#if defined(_WIN32)
			LARGE_INTEGER tick;
#elif defined(__linux__)
			timespec tick;
#endif
			UINT32 count;
			unsigned ddos : 1;
		}ConnectHistory, *PConnectHistory;

		typedef struct _connection_live {
			void clear() {
				memset(this, 0, sizeof(_connection_live));
#if defined(_WIN32)
				sock = INVALID_SOCKET;
#elif defined(__linux__)
				sock.fd = INVALID_SOCKET;
				sock.connect_time.tv_sec = 0;
				sock.connect_time.tv_nsec = 0;
#endif
			};
			_connection_live* next;
			UINT32 ip;
#if defined(_WIN32)
			LARGE_INTEGER tick;
#elif defined(__linux__)
			timespec tick;
#endif
			SOCKET sock;
		}ConnectLive, *PConnectLive;

		typedef struct _access_control {
			UINT32 ip;
			UINT32 mask;
		}AccessControl, *PAccessControl;

		enum _aco {
			ACO_DENY_ALLOW,
			ACO_ALLOW_DENY,
			ACO_MUTUAL_FAILURE
		};

	public:
		// ip rules, if enabled, DDoS Protection is also enabled
		bool ip_rules;

	private:
		// Connection history, ip & 0xFFFF
		PConnectHistory connect_history[0x10000];

		// Live connection, ip & 0xFFFF
		PConnectLive connect_live[0x10000];

		// Access Control
		std::vector < AccessControl > access_allow;
		std::vector < AccessControl > access_deny;
		int access_order;

		// DDoS Protection
		UINT32 ddos_count;
		UINT32 ddos_interval;
		UINT32 ddos_autoreset;

		// Limit of connection per IP
		UINT32 limit_connection_per_ip;

#if defined(_WIN32)
		DWORD dwThread;			// ID Thread Timer
		HANDLE hThread;			// Handle Thread Timer
#elif defined(__linux__)
		thread *hThread;		// Handle Thread Timer
#endif

#if defined(_WIN32)
		static DWORD WINAPI pThread_Timer(LPVOID lpParameter);			// Thread that calls the connec_check_clear function every 5 minutes
#elif defined(__linux__)
		static void* pThread_Timer(LPVOID lpParameter);			// Thread that calls the connec_check_clear function every 5 minutes
#endif

#if defined(_WIN32)
		LARGE_INTEGER gettick();										// Get the current QueryPerformanceCounter
#elif defined(__linux__)
		timespec gettick();												// Get the current QueryPerformanceCounter
#endif

#if defined(_WIN32)
		LONG volatile m_thread_check_on;								// atomic variable on while thread condition on or off
#elif defined(__linux__)
		int32_t volatile m_thread_check_on;								// atomic variable on while thread condition on or off
#endif

	public:
		int connect_check(SOCKET _sock, UINT32 ip);						// Check whether the ip can connect

		int connect_check2(SOCKET _sock, int _connect_ok, UINT32 _ip);	// Check whether the ip can have more connections than allowed

		bool connect_check_continue();									// Check whether to continue with the thread_timer

		void thread_connect_check_off();								// Turn off the connect_check thread, by modifying the atomic variable

		UINT32 getDDoS_AutoReset();										// Get Interval DDoS Auto Reset history

		void reload_config_file();										// Read the socket configuration file again

		static int getConnectTime(SOCKET _sock);						// [Static, does not need the socket class to run this function] Get the socket connection time from the system

	private:
#if defined(_WIN32)
		int connect_check_clear(LARGE_INTEGER tick);					// Clear the connection history entries that have exceeded the allowed time
		int connect_live_check_clear(LARGE_INTEGER tick);				// Clear the connection history with the same connected ip
#elif defined(__linux__)
		int connect_check_clear(timespec tick);							// Clear the connection history entries that have exceeded the allowed time
		int connect_live_check_clear(timespec tick);					// Clear the connection history with the same connected ip
#endif

		int connect_check_is_live();									// Remove the connections that are no longer connected from the linked list

		void close_connect_history();									// clear and close the connection history

		void close_connect_live();										// Clear and close live connections

		void close_access_allow();										// clear the access allow vector

		void close_acces_deny();										// clear the access deny vector

		int access_ipmask(const char* str, PAccessControl acc);			// convert from ip str to ip mask

		void config_read(const char* file_name);						// Read the socket configuration
		
	protected:
		SOCKET m_socket;
		SOCKADDR_IN m_sockaddr;

#if defined(_WIN32)
		CRITICAL_SECTION m_cs_ip;										// Critical Section for the connection history
		CRITICAL_SECTION m_cs_ipx;										// Critical Section for the live connections
#elif defined(__linux__)
		pthread_mutex_t m_cs_ip;
		pthread_mutex_t m_cs_ipx;
#endif
	};
}

#endif