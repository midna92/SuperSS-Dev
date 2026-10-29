// File cmd_rank_registry_info.hpp
// Created on 16/06/2020 at 15:32 by Acrisio
// Definition of the class CmdRankRegistryInfo

#pragma once
#ifndef _STDA_CMD_RANK_REGISTRY_INFO_HPP
#define _STDA_CMD_RANK_REGISTRY_INFO_HPP

#include "../../Projeto IOCP/PANGYA_DB/pangya_db.h"
#include "../UTIL/rank_registry.hpp"

namespace stdA {
	class CmdRankRegistryInfo : public pangya_db {
		public:
			explicit CmdRankRegistryInfo(bool _waiter = false);
			virtual ~CmdRankRegistryInfo();

			RankEntry& getInfo();

		protected:
			void lineResult(result_set::ctx_res *_result, uint32_t _index_result) override;
			response* prepareConsulta(database& _db) override;

			// Class Name
			std::string _getName() override { return "CmdRankRegistryInfo"; };
			std::wstring _wgetName() override { return L"CmdRankRegistryInfo"; };

		private:
			RankEntry m_entry;

			const char* m_szConsulta = "pangya.ProcGetRankRegistryInfo";
	};
}

#endif // !_STDA_CMD_RANK_REGISTRY_INFO_HPP
