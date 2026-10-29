// File cmd_daily_quest_info.hpp
// Created on 07/04/2018 at 20:008 by Acrisio
// Definition of the class CmdDailyQuestInfo

#pragma once
#ifndef _STDA_CMD_DAILY_QUEST_INFO_HPP
#define _STDA_CMD_DAILY_QUEST_INFO_HPP

#include "../../Projeto IOCP/PANGYA_DB/pangya_db.h"
#include "../TYPE/pangya_game_st.h"

namespace stdA {
	class CmdDailyQuestInfo : public pangya_db {
		public:
			explicit CmdDailyQuestInfo(bool _waiter = false);
			virtual ~CmdDailyQuestInfo();

			DailyQuestInfo& getInfo();
			void setInfo(DailyQuestInfo& _dqi);

		protected:
			void lineResult(result_set::ctx_res* _result, uint32_t _index_result) override;
			response* prepareConsulta(database& _db) override;

			// get Class name
			virtual std::string _getName() override { return "CmdDailyQuestInfo"; };
			virtual std::wstring _wgetName() override { return L"CmdDailyQuestInfo"; };

		private:
			DailyQuestInfo m_dqi;

			const char* m_szConsulta = "SELECT achieve_quest_1, achieve_quest_2, achieve_quest_3, Reg_Date FROM pangya.pangya_daily_quest";
	};
}

#endif // !_STDA_CMD_DAILY_QUEST_INFO_HPP
