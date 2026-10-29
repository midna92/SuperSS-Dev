// File cmd_comet_refill_info.cpp
// Created on 08/07/2018 at 22:49 by Acrisio
// Implementation of the class CmdCometRefillInfo

#if defined(_WIN32)
#pragma pack(1)
#endif

#include "cmd_comet_refill_info.hpp"
#include "../../Projeto IOCP/UTIL/exception.h"
#include "../../Projeto IOCP/TYPE/stda_error.h"
#include "../../Projeto IOCP/UTIL/message_pool.h"

using namespace stdA;

CmdCometRefillInfo::CmdCometRefillInfo(bool _waiter) : pangya_db(_waiter), m_ctx_cr() {
}

CmdCometRefillInfo::~CmdCometRefillInfo() {
}

void CmdCometRefillInfo::lineResult(result_set::ctx_res* _result, uint32_t /*_index_result*/) {

	checkColumnNumber(3, (uint32_t)_result->cols);

	ctx_comet_refill ctx_cr{ 0 };

	ctx_cr._typeid = IFNULL(atoi, _result->data[0]);
	ctx_cr.qntd_range.min = (unsigned short)IFNULL(atoi, _result->data[1]);
	ctx_cr.qntd_range.max = (unsigned short)IFNULL(atoi, _result->data[2]);

	auto it = m_ctx_cr.find(ctx_cr._typeid);

	if (it == m_ctx_cr.end())	// Not in the map, add it to the map
		m_ctx_cr[ctx_cr._typeid] = ctx_cr;
	else // already in the map, but do not update it because it is duplicated in the  DB
		_smp::message_pool::getInstance().push(new message("[CmdCometRefillInfo::lineResult][WARNING] tem comet refill[TYPEID=" + std::to_string(ctx_cr._typeid) + "] duplicado no banco de dados.", CL_FILE_LOG_AND_CONSOLE));
}

response* CmdCometRefillInfo::prepareConsulta(database& _db) {

	auto r = procedure(_db, m_szConsulta, "");

	checkResponse(r, "nao conseguiu pegar o comet refill info");

	return r;
}

std::map< uint32_t, ctx_comet_refill > CmdCometRefillInfo::getInfo() {
	return m_ctx_cr;
}
