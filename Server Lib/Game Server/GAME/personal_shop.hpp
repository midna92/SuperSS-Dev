// File personal_shop.hpp
// Created on 09/06/2018 at 09:56 by Acrisio
// Definition of the PersonalShop class

#pragma once
#ifndef _STDA_PERSONAL_SHOP_HPP
#define _STDA_PERSONAL_SHOP_HPP

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <unistd.h>
#endif

#include "../SESSION/player.hpp"

#include <vector>
#include <string>

#define LIMIT_VISIT_ON_SAME_TIME	15

namespace stdA {
	class PersonalShop {
		public:
			enum STATE : unsigned {
				OPEN_EDIT,
				OPEN,
			};

			struct Locker {
				public:
					Locker(PersonalShop& _shop);
					~Locker();

				protected:
					PersonalShop& m_shop;
			};

		public:
			PersonalShop(player& _session);
			~PersonalShop();

			// Gets
			std::string& getName();
			uint32_t getVisitCount();
			uint64_t getPangSale();
			player& getOwner();

			STATE getState();

			uint32_t getCountItem();

			std::vector< player* >& getClients();

			// Sets
			void setName(std::string& _name);
			void setState(STATE _state);

			void clearItem();
			void pushItem(PersonalShopItem& _psi);
			void deleteItem(PersonalShopItem& _psi);

			void putItemOnPacket(packet& _p);

			// Find
			PersonalShopItem* findItemById(int32_t _id);
			PersonalShopItem* findItemByIndex(uint32_t _index);
			int32_t findItemIndexById(int32_t _id);

			player* findClientByUID(uint32_t _uid);
			int32_t findClientIndexByUID(uint32_t _uid);

			// Visit
			void addClient(player& _session);
			void deleteClient(player& _session);

			void buyItem(player& _session, PersonalShopItem& _psi);

		protected:
			void _lock();
			void _unlock();

		private:
			void destroy();

			inline void shop_broadcast(packet& _p, session *_s, unsigned char _debug);
			inline void shop_broadcast(std::vector< packet* > _v_p, session *_s, unsigned char _debug);

		protected:
			std::string m_name;		// Shop Name
			player& m_owner;		// Shop Owner

			STATE m_state;			// shop state, open or editing

			uint32_t m_visit_count;	// Number of visitors who visited the shop

			uint64_t m_pang_sale;	// pangs in the cash box

		protected:
			std::vector< PersonalShopItem > v_item;		// Shop Items

		protected:
#if defined(_WIN32)
			CRITICAL_SECTION m_cs;
#elif defined(__linux__)
			pthread_mutex_t m_cs;
#endif
			std::vector< player* > v_open_shop_visit;	// The visitors who have the shop open
	};
}

#endif // !_STDA_PERSONAL_SHOP_HPP
