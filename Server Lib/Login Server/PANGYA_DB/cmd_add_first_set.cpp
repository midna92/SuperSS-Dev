// File cmd_add_first_set.cpp
// Created on 01/04/2018 at 17:36 by Acrisio
// Implementation of the CmdAddFirstSet class

#if defined(_WIN32)
#pragma pack(1)
#endif

#include "cmd_add_first_set.hpp"

using namespace stdA;

CmdAddFirstSet::CmdAddFirstSet(bool _waiter) : pangya_db(_waiter), m_uid(0) {
}

CmdAddFirstSet::CmdAddFirstSet(uint32_t _uid, bool _waiter) : pangya_db(_waiter), m_uid(_uid) {
}

CmdAddFirstSet::~CmdAddFirstSet() {
}

void CmdAddFirstSet::lineResult(result_set::ctx_res* /*_result*/, uint32_t /*_index_result*/) {

	// Not used because it is INSERT and UPDATE
	return;
}

response* CmdAddFirstSet::prepareConsulta(database& _db) {

	auto r = procedure(_db, m_szConsulta, std::to_string(m_uid));

	checkResponse(r, "nao conseguiu add first set do player: " + std::to_string(m_uid));

	return r;
}

uint32_t CmdAddFirstSet::getUID() {
	return m_uid;
}

void CmdAddFirstSet::setUID(uint32_t _uid) {
	m_uid;
}
