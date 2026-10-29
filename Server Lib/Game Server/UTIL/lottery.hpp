// File lottery.hpp
// Created on 24/06/2018 at 11:53 by Acrisio
// Definition of the Lottery class

#pragma once
#ifndef _STDA_LOTTERY_HPP
#define _STDA_LOTTERY_HPP

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include "../../Projeto IOCP/UTIL/WinPort.h"
#endif

#include <vector>
#include <map>

#include <memory.h>

namespace stdA {
	class Lottery {
		public:
			struct LotteryCtx {
				void clear() { memset(this, 0, sizeof(LotteryCtx)); };
				uint32_t prob;	// Probability
				size_t value;
				uint64_t offset[2];	// 0 start, 1 end
				unsigned char active : 1, : 0;	// 0 or 1 active
			};

		public:
			Lottery(uint64_t _value_rand);
			~Lottery();
			
			void clear();	// Clear Ctx

			void push(LotteryCtx& _lc);
			void push(uint32_t _prob, size_t _value);

			uint64_t getLimitProbilidade();

			// Returns the number of items available to draw
			uint32_t getCountItem();

			// Deletes the Drawn Item, so that it does not come out again, if true is passed
			LotteryCtx* spinRoleta(bool _remove_item_draw = false);

		protected:
			void initialize(uint64_t _value_rand);

			void fill_roleta();
			void clear_roleta();

			void remove_draw_item(LotteryCtx* _lc);

			void shuffle_values_rand();

		private:
			std::map< uint64_t, LotteryCtx* > m_roleta;

			std::vector< LotteryCtx > m_ctx;
			std::vector< uint64_t > m_rand_values;

			uint64_t m_prob_limit;
	};
}

#endif // !_STDA_LOTTERY_HPP
