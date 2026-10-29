// File tourney_base_type.hpp
// Created on 18/08/2018 at 15:19 by Acrisio
// Definition of the types used in the TourneyBase class

#pragma once
#ifndef _STDA_TOURNEY_BASE_TYPE_HPP
#define _STDA_TOURNEY_BASE_TYPE_HPP

#include <memory>

namespace stdA {

#if defined(__linux__)
#pragma pack(1)
#endif

	struct Medal {
		Medal(uint32_t _ul = 0u) {
			clear();
		};
		void clear() {
			oid = -1;
			item_typeid = 0u;
		};
		int32_t oid;
		uint32_t item_typeid;
	};

#if defined(__linux__)
#pragma pack()
#endif
}

#endif // !_STDA_TOURNEY_BASE_TYPE_HPP
