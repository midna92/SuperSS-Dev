// File cmd_register_server.hpp
// Created on 25/03/2018 at 21:20 by Acrisio
// Definition of class CmdRegisterServer

#pragma once
#ifndef _STDA_CMD_REGISTER_SERVER_HPP
#define _STDA_CMD_REGISTER_SERVER_HPP

#include "pangya_db.h"
#include "../TYPE/pangya_st.h"

namespace stdA {
    class CmdRegisterServer : public pangya_db {
        public:
            explicit CmdRegisterServer(bool _waiter = false);
            CmdRegisterServer(ServerInfoEx& _si, bool _waiter = false);
            virtual ~CmdRegisterServer();

            ServerInfoEx& getInfo();
            void setInfo(ServerInfoEx& _si);

        protected:
            void lineResult(result_set::ctx_res* _result, uint32_t _index_result) override;
            response* prepareConsulta(database& _db) override;

			// get Class name
			virtual std::string _getName() override { return "CmdRegisterServer"; };
			virtual std::wstring _wgetName() override { return L"CmdRegisterServer"; };

        private:
            ServerInfoEx m_si;

            const char* m_szConsulta = "pangya.ProcRegServer_New";
    };
}

#endif // !_STDA_CMD_REGISTER_SERVER_HPP