// File string_convert.hpp
// Created on 21/11/2020 at 10:41 by Acrisio
// Definition and implementation of string converters

#pragma once
#ifndef _STDA_STRING_CONVERT_HPP
#define _STDA_STRING_CONVERT_HPP

#include <string>

namespace stdA {

	inline void copyStrToSzString(std::string _str, char* _szStr) {

		if (_szStr == nullptr)
			return;

		if (_str.empty())
			return;

#if defined(_WIN32)
		memcpy_s(_szStr, _str.length(), _str.data(), _str.length());
#elif defined(__linux__)
		memcpy(_szStr, _str.data(), _str.length());
#endif

		_szStr[_str.length()] = '\0';
	};
}

#endif // !_STDA_STRING_CONVERT_HPP
