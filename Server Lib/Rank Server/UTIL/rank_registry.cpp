// File rank_registry.cpp
// Created on 16/06/2020 at 14:50 by Acrisio
// Implementation of the class RankRegistry

#if defined(_WIN32)
#pragma pack(1)
#endif

#if defined(_WIN32)
#include <WinSock2.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#endif

#include "rank_registry.hpp"

using namespace stdA;

RankRegistry::RankRegistry() 
	: m_uid(0u), m_current_position(0u), m_last_position(0u), m_value(0) {
}

RankRegistry::RankRegistry(uint32_t _uid, uint32_t _current_position, uint32_t _last_position, int32_t _valuen)
	: m_uid(_uid), m_current_position(_current_position), m_last_position(_last_position) {
}

RankRegistry::~RankRegistry() {
	clear();
}

void RankRegistry::clear() {

	m_uid = 0u;
	m_current_position = 0u;
	m_last_position = 0u;
	m_value = 0l;
}

void RankRegistry::toPacket(packet& _packet) {

	_packet.addUint32(m_uid);
	_packet.addUint32(m_current_position);
	_packet.addUint32(m_last_position);
	_packet.addInt32(m_value);
}

void RankRegistry::toCompactPacket(packet& _packet) {

	// Here if the rank goes over 10 thousand it is zeroed, because pangya only counts the top 10 thousand
	// But in the database it counts normally, since in the full toPacket it sends the rank even if it is greater than 10 thousand
	// Page, it only sends 5 thousand, it only shows pages of 5 thousand ranks (Records)

	// I will do it this way.
	// If the current rank is over 10 thousand, zero both
	// If the current rank is less than or equal to 10 thousand, send the last position even if it is greater than 10 thousand

	if (CHECK_LIMIT_RANK_POSITION_COMPACT_PACKET(m_current_position))
		_packet.addZeroByte(8u);	// Send zeros in Current Position and Last Position, because the current position is greater than 10 thousand
	else {
		_packet.addUint32(m_current_position);
		_packet.addUint32(m_last_position);
	}

	_packet.addInt32(m_value);
}

// Get
uint32_t& RankRegistry::getUID() {
	return m_uid;
}

uint32_t& RankRegistry::getCurrentPosition() {
	return m_current_position;
}

uint32_t& RankRegistry::getLastPosition() {
	return m_last_position;
}

int32_t& RankRegistry::getValue() {
	return m_value;
}

// Set
void RankRegistry::setUID(uint32_t _uid) {
	m_uid = _uid;
}

void RankRegistry::setCurrentPosition(uint32_t _current_position) {
	m_current_position = _current_position;
}

void RankRegistry::setLastPosition(uint32_t _last_position) {
	m_last_position = _last_position;
}

void RankRegistry::setValue(int32_t _value) {
	m_value = _value;
}
