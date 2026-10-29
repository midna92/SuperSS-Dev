// File cmd_achievement_check.hpp
// Created on 07/04/2018 at 19:00 by Acrisio
// Definition of the class CmdCheckAchievement

#pragma once
#ifndef _STDA_CMD_ACHIEVEMENT_CHECK_HPP
#define _STDA_CMD_ACHIEVEMENT_CHECK_HPP

#include "../../Projeto IOCP/PANGYA_DB/pangya_db.h"

namespace stdA {
	class CmdCheckAchievement : public pangya_db {
		public:
			explicit CmdCheckAchievement(bool _waiter = false);
			CmdCheckAchievement(uint32_t _uid, bool _waiter = false);
			virtual ~CmdCheckAchievement();

			uint32_t getUID();
			void setUID(uint32_t _uid);

			bool getLastState();

		protected:
			void lineResult(result_set::ctx_res* _result, uint32_t _index_result) override;
			response* prepareConsulta(database& _db) override;

			// get Class name
			virtual std::string _getName() override { return "CmdCheckAchievement"; };
			virtual std::wstring _wgetName() override { return L"CmdCheckAchievement"; };

		private:
			uint32_t m_uid;
			bool m_check;

			const char* m_szConsulta = "pangya.ProcCheckAchievement";
	};
}

#endif // !_STDA_CMD_ACHIEVEMENT_CHECK_HPP
