// File cmd_update_daily_quest_user.hpp
// Created on 11/11/2018 at 15:56 by Acrisio
// Definition of class CmdUpdateDailyQuestUser

#pragma once
#ifndef _STDA_CMD_UPDATE_DAILY_QUEST_USER_HPP
#define _STDA_CMD_UPDATE_DAILY_QUEST_USER_HPP

#include "../../Projeto IOCP/PANGYA_DB/pangya_db.h"
#include "../TYPE/pangya_game_st.h"

namespace stdA {
	class CmdUpdateDailyQuestUser : public pangya_db {
		public:
			explicit CmdUpdateDailyQuestUser(bool _waiter = false);
			CmdUpdateDailyQuestUser(uint32_t _uid, DailyQuestInfoUser& _dqiu, bool _waiter = false);
			virtual ~CmdUpdateDailyQuestUser();

			uint32_t getUID();
			void setUID(uint32_t _uid);

			DailyQuestInfoUser& getInfo();
			void setInfo(DailyQuestInfoUser& _dqiu);

		protected:
			void lineResult(result_set::ctx_res* _result, uint32_t _index_result) override;
			response* prepareConsulta(database& _db) override;

			std::string _getName() override { return "CmdUpdateDailyQuestUser"; };
			std::wstring _wgetName() override { return L"CmdUpdateDailyQuestUser"; };

		private:
			uint32_t m_uid;
			DailyQuestInfoUser m_dqiu;

			const char* m_szConsulta = "pangya.ProcUpdateDailyQuestUser";
	};
}

#endif // !_STDA_CMD_UPDATE_DAILY_QUEST_USER_HPP
