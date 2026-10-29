// File cmd_delete_email.hpp
// Created on 24/03/2018 at 15:33 by Acrisio
// Definition of the class CmdDeleteEmail

#pragma once
#ifndef _STDA_CMD_DELETE_EMAIL_HPP
#define _STDA_CMD_DELETE_EMAIL_HPP

#include "../../Projeto IOCP/PANGYA_DB/pangya_db.h"

namespace stdA {
	class CmdDeleteEmail : public pangya_db {
		public:
			explicit CmdDeleteEmail(bool _waiter = false);
			CmdDeleteEmail(uint32_t _uid, int32_t *_email_id, uint32_t _count, bool _waiter = false);
			virtual ~CmdDeleteEmail();

			uint32_t getUID();
			void setUID(uint32_t _uid);

			int32_t* getEmailID();
			void setEmailID(int32_t *_email_id, uint32_t _count);

			uint32_t getCount();

		protected:
			void lineResult(result_set::ctx_res* _result, uint32_t _index_result) override;
			response* prepareConsulta(database& _db) override;

			// get Class name
			virtual std::string _getName() override { return "CmdDeleteEmail"; };
			virtual std::wstring _wgetName() override { return L"CmdDeleteEmail"; };

		private:
			uint32_t m_uid;
			int32_t *m_email_id;
			uint32_t m_count;

			const char* m_szConsulta[3] = { "UPDATE pangya.pangya_gift_table SET valid = 0 WHERE uid = ", " AND Msg_ID IN(", ")" };
	};
}

#endif // !_STDA_CMD_DELETE_EMAIL_HPP
