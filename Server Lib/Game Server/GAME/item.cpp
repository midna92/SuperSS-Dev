// File item.cpp
// Created on 24/02/2018 at 19:12 by Acrisio
// Implementation of class item

#if defined(_WIN32)
#pragma pack(1)
#endif

#include "item.hpp"
#include <memory>

#include <memory.h>

using namespace stdA;

item::item() {
	memset(this, 0, sizeof(item));
};

item::~item() {

};
