// File exception.cpp
// Created on 02/03/2017 by Acrisio
// Implementation of the class exception

#if defined(_WIN32)
#pragma pack(1)
#endif

#include "exception.h"
#include "../../Projeto IOCP/UTIL/string_util.hpp"

using namespace stdA;

exception::exception(std::string message_error, uint32_t code_error) {
	m_message_error = message_error;
	m_code_error = code_error;
	
	//sprintf_s(tmp, "%s\t Error Code: %zd", m_message_error.c_str(), m_code_error);
	// Later I have to check what this %z is, I think it is something or I put it there by accident

	m_message_error_full = m_message_error + "\t Error Code: " + std::to_string(m_code_error);
};

exception::exception(std::wstring message_error, uint32_t code_error) {
	m_code_error = code_error;

	//m_message_error.assign(message_error.begin(), message_error.end());	// Convert WSTRING to string
	m_message_error = WcToMb(message_error);

	m_message_error_full = m_message_error + "\t Error Code: " + std::to_string(m_code_error);
};

exception::~exception() {
	m_message_error.shrink_to_fit();
	m_code_error = 0;
};

const std::string exception::getMessageError() {
	return m_message_error;
};

const uint32_t exception::getCodeError() {
	return m_code_error;
};

const std::string exception::getFullMessageError() {
	return m_message_error_full;
};