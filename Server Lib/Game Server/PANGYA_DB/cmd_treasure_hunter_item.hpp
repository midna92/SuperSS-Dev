// File cmd_treasure_hunter_item.hpp
// Created on 06/09/2018 at 20:21 by Acrisio
// Definition of the class CmdTreasureHunterItem

#pragma once
#ifndef _STDA_CMD_TREASURE_HUNTER_ITEM_HPP
#define _STDA_CMD_TREASURE_HUNTER_ITEM_HPP

#include "../../Projeto IOCP/PANGYA_DB/pangya_db.h"
#include "../TYPE/pangya_game_st.h"
#include <vector>

namespace stdA {
	class CmdTreasureHunterItem : public pangya_db {
		public:
			CmdTreasureHunterItem(bool _waiter = false);
			virtual ~CmdTreasureHunterItem();

			std::vector< TreasureHunterItem >& getInfo();

		protected:
			void lineResult(result_set::ctx_res *_result, uint32_t _index_result) override;
			response* prepareConsulta(database& _db) override;

			// get Class name
			virtual std::string _getName() override { return "CmdTreasureHunterItem"; };
			virtual std::wstring _wgetName() override { return L"CmdTreasureHunterItem"; };

		private:
			std::vector< TreasureHunterItem > v_thi;

			const char *m_szConsulta = "SELECT typeid, quantidade, probabilidade, tipo, flag FROM pangya.pangya_treasure_item";
	};
}

#endif // !_STDA_CMD_TREASURE_HUNTER_ITEM_HPP
