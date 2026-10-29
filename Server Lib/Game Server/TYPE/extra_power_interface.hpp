// File extra_power_interface.hpp
// Created on 03/10/2020 at 15:21 by Acrisio
// Definition of class Interface ExtraPower

#pragma once
#ifndef _STDA_EXTRA_POWER_INTERFACE_HPP
#define _STDA_EXTRA_POWER_INTERFACE_HPP

namespace stdA {

	class IExtraPower {

		public:
			virtual float getTotal(unsigned char _psf) = 0;
	};
}

#endif // !_STDA_EXTRA_POWER_INTERFACE_HPP
