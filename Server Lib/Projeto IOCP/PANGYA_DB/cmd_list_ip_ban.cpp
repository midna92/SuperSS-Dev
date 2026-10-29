// File cmd_list_ip_ban.cpp
// Created on 30/03/2018 at 10:39 by Acrisio
// Implementation of class CmdListIPBan

#if defined(_WIN32)
#pragma pack(1)
#endif

#if defined(_WIN32)
#include <WS2tcpip.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#endif

#include "cmd_list_ip_ban.hpp"

using namespace stdA;

CmdListIPBan::CmdListIPBan(bool _waiter) : pangya_db(_waiter), v_ip_ban() {
}

CmdListIPBan::~CmdListIPBan() {
}

void CmdListIPBan::lineResult(result_set::ctx_res* _result, uint32_t /*_index_result*/) {

	checkColumnNumber(2, (uint32_t)_result->cols);

	IPBan pb{ 0 };

	std::string ip = "", mask = "";
	size_t offset = std::string::npos;

	int error = 1;

	if (is_valid_c_string(_result->data[0]))
		ip = _result->data[0];
	if (is_valid_c_string(_result->data[1]))
		mask = _result->data[1];

	// Check whether it is an IP RANGE
	if ((offset = ip.find('/')) == std::string::npos && ip.size() > 15)
		return;
	else {

		// IP Range
		if (offset != std::string::npos) {
			
			pb.type = IPBan::_TYPE::IP_BLOCK_RANGE;

			mask = ip.substr(offset + 1);
			ip = ip.substr(0, offset);

			error = inet_pton(AF_INET, ip.c_str(), &pb.ip);

			error = inet_pton(AF_INET, mask.c_str(), &pb.mask);
		
		}else {

			// Normal IP
			error = inet_pton(AF_INET, ip.c_str(), &pb.ip);

			error = inet_pton(AF_INET, mask.c_str(), &pb.mask);
		}
	}

	// Error 1 Success has a valid ip and mask
	if (error == 1) {

		pb.ip = ntohl(pb.ip);
		pb.mask = ntohl(pb.mask);

		v_ip_ban.push_back(pb);
	}
}

response* CmdListIPBan::prepareConsulta(database& _db) {

	v_ip_ban.clear();
	v_ip_ban.shrink_to_fit();

	auto r = consulta(_db, m_szConsulta);

	checkResponse(r, "nao conseguiu pegar a lista de ip(s) banidos");

	return r;
}

std::vector< IPBan >& CmdListIPBan::getListIPBan() {
	return v_ip_ban;
}
