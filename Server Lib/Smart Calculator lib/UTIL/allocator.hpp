// File allocator.hpp
// Created on 21/11/2020 at 10:38 by Acrisio
// Definition and implementation of allocators

#pragma once
#ifndef _STDA_ALLOCATOR_HPP
#define _STDA_ALLOCATOT_HPP

#include <memory>

namespace stdA {

	template<typename _Type> _Type allocType(size_t _chunk) {
		return reinterpret_cast< _Type >( malloc(_chunk) );
	};
}

#endif // !_STDA_ALLOCATOR_HPP
