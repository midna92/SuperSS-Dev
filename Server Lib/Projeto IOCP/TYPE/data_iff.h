// File data_iff.h
// Created on 03/10/2017 by Acrisio
// Definition of the pangya iff structures

#if defined(_WIN32)
#pragma pack(1)
#endif

#pragma once
#ifndef _STDA_IFF_DATA_IFF_H
#define _STDA_IFF_DATA_IFF_H

#include <memory.h>
#include <string>
#include "../UTIL/hex_util.h"
#include "../UTIL/util_time.h"
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include "../UTIL/WinPort.h"
#endif

#include <algorithm>
#include <numeric>

#define EMPTY_ARRAY_PRICE(_price) (bool)!std::count_if(_price, (_price + (sizeof(_price) / sizeof(_price[0]))), [](auto& el) { return el != 0; })
#define SUM_ARRAY_PRICE_ULONG(_price) (unsigned int)std::accumulate(_price, (_price + (sizeof(_price) / sizeof(_price[0]))), 0u)

// Here so the pack is not done in other libraries
#if defined(__linux__)
#pragma pack(1)
#endif

namespace stdA {
	namespace IFF {
		// Head IFF
		struct Head {
			void clear() {
				memset(this, 0, sizeof(Head));
			};
			unsigned short count_element;       // Number of Objects on file
			unsigned short flag_ligacao;        // Link flag to other IFFs
			unsigned int  version;				// IFF version
		};

		// Shop Data
		struct ShopDados {
			void clear() {
				memset(this, 0, sizeof(ShopDados));
			};
			unsigned int  price;
			unsigned int  sale_price;			// By TH S4 - (Sale Price) Desconto (price - sale_price) = Desconto
			unsigned int  sell_price;			// By TH S4 - (Used Price) Resale price of the item, but it was made in the season 1 beta, and later disabled
			struct FlagShop {
				struct _8bits {
					unsigned char bit0 : 1;
					unsigned char bit1 : 1;
					unsigned char bit2 : 1;
					unsigned char bit3 : 1;
					unsigned char bit4 : 1;
					unsigned char bit5 : 1;
					unsigned char bit6 : 1;
					unsigned char bit7 : 1;
				};
				union {
					unsigned short us_flag_shop;
					unsigned char uc_bytes[2];
					struct _stFlagShop {
						unsigned char is_cash : 1;
						unsigned char can_send_mail_and_personal_shop : 1;
						unsigned char can_dup : 1;
						unsigned char unknown : 1;
						unsigned char block_mail_and_personal_shop : 1;
						unsigned char is_saleable : 1;	// Pang(Only Purchase, CP Gift and Purchase) (is_saleable and is_giftable become a flag for Purchase only, CP or Pang)
						unsigned char is_giftable : 1;	// CP can only be gifted (is_saleable and is_giftable become a flag for Purchase only, CP or Pang)
						unsigned char only_display : 1;	// Display only in the shop
					} stFlagShop;
					struct _stIconShop {
						unsigned short : 8;	// Flag Shop is the structure above, I have to do this because it is inside a union
						unsigned short is_new : 1;
						unsigned short is_hot : 1;
						unsigned short unknown_bit2 : 1;
						unsigned short unknown_bit3 : 1;
						unsigned short unknown_bit4 : 1;
						unsigned short unknown_bit5 : 1;
						unsigned short unknown_bit6 : 1;
						unsigned short unknown_bit7 : 1;
					} stIconShop;
					//_8bits _byte[2];
					// Byte 1
/*#define IFF_cash			_byte[0].bit0	// 1 point 0 pang
#define IFF_personal_shop	_byte[0].bit1	// this flag means it can be sold in the personal shop and sent by email
#define IFF_dup_normal		_byte[0].bit2	// this is the dup flag, I think
#define IFF_gift			_byte[0].bit3	// I do not know
#define IFF_time			_byte[0].bit4	// This one blocks the Item from being sent in the personal shop and mail
#define IFF_shop_sale		_byte[0].bit5	// purchase and gift (this flag with bit6, becomes purchase only)
#define IFF_condition		_byte[0].bit6	// gift only (this flag with bit5, becomes purchase only)
#define IFF_item_pack		_byte[0].bit7	// view (Display) only // (I THINK) I only saw this bit active in the 3 coupon gacha, item pack is a normal item that works as a pack(setItem), so one must check which is the single of the item to change the typeid
					// Byte 2
#define IFF_novo			_byte[1].bit1
#define IFF_hot				_byte[1].bit2*/
				} uFlagShop;
				struct TimeShop {
					/*union {
						unsigned char uc_byte;
						FlagShop::_8bits _bits;
						// Byte 1
#define IFF_time_limit		_bits.bit0
					} uTimeShop;*/
					unsigned char active : 1;	// Time-limited item
					unsigned char dia;			// Time in Days, 1, 7, 15, 30 and 365 && 0xFF becomes 0x6D, because 0x16D = 365
				} time_shop;
			};
			FlagShop flag_shop;
		};

		// Tiki Shop Data
		struct TikiShopDados {
			void clear() {
				memset(this, 0, sizeof(TikiShopDados));
			};
			bool isActived() {
				return (tipo_tiki_shop == 1u || tipo_tiki_shop == 2u || tipo_tiki_shop == 3u) && tiki_pang > 0u && milage_pts > 0u;
			};
			unsigned int  qnt_per_tiki_pts;
			unsigned int  tiki_pts;
			unsigned short milage_pts;
			unsigned short bonus_prob;
			unsigned short bonus[2];            // bonus[0] min_bonus, bonus[1] max_bonus
			unsigned int  tipo_tiki_shop;
			unsigned int  tiki_pang;
		};

		// Date Data
		struct DateDados {
			void clear() {
				memset(this, 0, sizeof(DateDados));
			};
			unsigned int  active_date : 1, : 0;	// 1 Activated, 0 Deactivated
			SYSTEMTIME date[2];					// Date[0] Start, Date[1] End
		};

		// Base IFF
		struct Base {
			void clear() {
				memset(this, 0, sizeof(Base));
			};
			unsigned int  active;
			unsigned int  _typeid;
			char name[64];
			struct stLevel {
				void clear() { memset(this, 0, sizeof(stLevel)); };
				bool goodLevel(unsigned char _level) {
					if (is_max && _level <= level)
						return true;
					else if (!is_max && _level >= level)
						return true;

					return false;
				};
				unsigned char level : 7;
				unsigned char is_max : 1;
			};
			stLevel level;			// it is signed here
			char icon[43];
			ShopDados shop;
			TikiShopDados tiki;
			DateDados date;
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid)
					 + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					 + "\r\nName: " + name + "\r\n";
#else
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")\r\n";
#endif // _DEBUG
			};
		};

		// Item IFF
		struct Item : public Base {
			void clear() {
				memset(this, 0, sizeof(Item));
			};
			unsigned int  tipo_item;
			char mpet[40];
			short c[5];			// this one, when the item is time-limited, is the price for the days, 1, 7, 15, 30 and 365 days
			short point;		// By TH S4 - (Point) Here is the No Limit time price, but when purchasing it passes the price -1, but visually it shows the value that is here
		};

		// Achievment IFF
		struct Achievement : public Base {
			void clear() {
				memset(this, 0, sizeof(Achievement));
			};
			unsigned int  typeid_quest_index;
			unsigned int  achievement_tipo;
			char quest_name[10][129];
			unsigned short s_unknown;
			unsigned int  quest_typeid[10];
			unsigned int  l_unknown;
		};

		// QuestStuff IFF
		struct QuestStuff : public Base {
			void clear() {
				memset(this, 0, sizeof(QuestStuff));
			};
			struct CounterItem {
				unsigned int  _typeid[5];
				int qntd[5];
			};
			struct RewardItem {
				unsigned int  _typeid[3];
				unsigned int  qntd[3];
				unsigned int  time[3];	// !@[I THINK] this is also that
			};
			CounterItem counter_item;
			RewardItem reward_item;
			//unsigned char uc_unknown12[12];
		};

		// QuestItem IFF
		struct QuestItem : public Base {
			void clear() {
				memset(this, 0, sizeof(QuestItem));
			};
			unsigned int  ulUnknown;
			unsigned int  type;
			struct Quest {
				unsigned int  qntd;
				unsigned int  _typeid[10];
			};
			struct Reward {
				unsigned int  _typeid[2];
				unsigned int  qntd[2];
				unsigned int  time[2];		// !@[I THINK] this is also that
			};
			Quest quest;
			Reward reward;
			unsigned int  ulUnknown2;
		};

		// CounterItem IFF
		struct CounterItem : Base {
			void clear() {
				memset(this, 0, sizeof(CounterItem));
			};
			char info[88];
		};

		// Part IFF
		struct Part : public Base {
			// By S4 TH - (Category)
			enum PART_TYPE : unsigned int  {
				TOP,				// By TH S4 - (UPPER)
				BOTTOM,				// By TH S4 - (LOWER)
				HEAD,				// By TH S4 - (HEAD)
				ARM,				// By TH S4 - (ARM == arm) CLOVE
				FOOT,				// By TH S4 - (FOOT == foot) SHOE
				ETC,				// By TH S4 - (ETC) ACESSORY_OR_BASE
				SUB_LEG,			// By TH S4 - (SUB_LEG)
				UCC,				// By TH S4 - (UCC)
				UCC_BLANK,			// By TH S4 - (UCC_DRAW_ONLY)
				UCC_COPY,			// By TH S4 - (UCC_COPY_ONLY)
			};
			void clear() {
				memset(this, 0, sizeof(Part));
			};
			char mpet[40];
			PART_TYPE type_item;				// By S4 TH - (Category) the item type, 0, 2 normal, 8 and 9 UCC, 5 I think it is base or common Item
			union u_part_type {
				unsigned int  ul_part_type;
				struct {
					unsigned char slot0 : 1;
					unsigned char slot1 : 1;
					unsigned char slot2 : 1;
					unsigned char slot3 : 1;
					unsigned char slot4 : 1;
					unsigned char slot5 : 1;
					unsigned char slot6 : 1;
					unsigned char slot7 : 1;
					unsigned char slot8 : 1;
					unsigned char slot9 : 1;
					unsigned char slot10 : 1;
					unsigned char slot11 : 1;	// Wings Slot
					unsigned char slot12 : 1;
					unsigned char slot13 : 1;
					unsigned char slot14 : 1;
					unsigned char slot15 : 1;
					unsigned char slot16 : 1;
					unsigned char slot17 : 1;
					unsigned char slot18 : 1;
					unsigned char slot19 : 1;
					unsigned char slot20 : 1;
					unsigned char slot21 : 1;
					unsigned char slot22 : 1;
					unsigned char slot23 : 1;
				} _slots;
				char getSlot(unsigned int  _i) {
					switch (_i) {
					case 0:
						return (char)_slots.slot0;
					case 1:
						return (char)_slots.slot1;
					case 2:
						return (char)_slots.slot2;
					case 3:
						return (char)_slots.slot3;
					case 4:
						return (char)_slots.slot4;
					case 5:
						return (char)_slots.slot5;
					case 6:
						return (char)_slots.slot6;
					case 7:
						return (char)_slots.slot7;
					case 8:
						return (char)_slots.slot8;
					case 9:
						return (char)_slots.slot9;
					case 10:
						return (char)_slots.slot10;
					case 11:
						return (char)_slots.slot11;
					case 12:
						return (char)_slots.slot12;
					case 13:
						return (char)_slots.slot13;
					case 14:
						return (char)_slots.slot14;
					case 15:
						return (char)_slots.slot15;
					case 16:
						return (char)_slots.slot16;
					case 17:
						return (char)_slots.slot17;
					case 18:
						return (char)_slots.slot18;
					case 19:
						return (char)_slots.slot19;
					case 20:
						return (char)_slots.slot20;
					case 21:
						return (char)_slots.slot21;
					case 22:
						return (char)_slots.slot22;
					case 23:
						return (char)_slots.slot23;
					default:
						return -1;
					}
				};
			};
			u_part_type position_mask;				// By S4 TH - (PosMask) Position Mask
			u_part_type hide_mask;					// By S4 TH - (HideMask) Hides the other parts of the slots
			char texture[3][40];					// 3 Texture
			char texture_org[3][40];				// By S4 TH - (OrgTex) 3 Org Textura(Original)
			unsigned short c[5];					// C = Stats[Control, Power, Spin e etc]
			unsigned short slot[5];					// Same as C, but this one is slot
			char equipable_with[40];				// By TH S4
			unsigned int sub_part[2];				// By TH S4
			unsigned short character_slot;			// By TH S4
			unsigned short caddie_slot;				// By TH S4 - (caddie_slot) Terceiro Caddie Card Slot
			unsigned short npc_slot;
			unsigned short point;
			unsigned int  valor_rental;
			unsigned int  is_beginners;				// Item for beginners
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nType: " + std::to_string(type_item)
					+ "\r\nPosition Slot	1	2	3	4	5	6	7	8	9	10	11	12"
					+ "\r\n		"	+ std::to_string((unsigned short)position_mask._slots.slot0) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot1) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot2) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot3) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot4) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot5) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot6) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot7) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot8) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot9) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot10) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot11) + "	"
					+ "\r\nPosition Slot	13	14	15	16	17	18	16	20	21	22	23	24"
					+ "\r\n		"	+ std::to_string((unsigned short)position_mask._slots.slot12) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot13) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot14) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot15) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot16) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot17) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot18) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot19) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot20) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot21) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot22) + "	"
								+ std::to_string((unsigned short)position_mask._slots.slot23) + "	"
					+ "\r\nHide Slot	1	2	3	4	5	6	7	8	9	10	11	12"
					+ "\r\n		"	+ std::to_string((unsigned short)hide_mask._slots.slot0) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot1) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot2) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot3) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot4) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot5) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot6) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot7) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot8) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot9) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot10) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot11) + "	"
					+ "\r\nHide Slot	13	14	15	16	17	18	16	20	21	22	23	24"
					+ "\r\n		"	+ std::to_string((unsigned short)hide_mask._slots.slot12) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot13) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot14) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot15) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot16) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot17) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot18) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot19) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot20) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot21) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot22) + "	"
								+ std::to_string((unsigned short)hide_mask._slots.slot23) + "	"
					+ "\r\nC:	" + std::to_string(c[0]) + "	" + std::to_string(c[1]) + "	"
							+ std::to_string(c[2]) + "	" + std::to_string(c[3]) + "	" + std::to_string(c[4])
					+ "\r\nSlot:	" + std::to_string(slot[0]) + "	" + std::to_string(slot[1]) + "	"
							+ std::to_string(slot[2]) + "	" + std::to_string(slot[3]) + "	" + std::to_string(slot[4]) 
					+ "\r\nSell Shop: " + std::to_string(shop.sell_price)
					+ "\r\nFlag Shop	1	2	3	4	5	6	7	8	9	10	11	12	13	14	15	16"
					+ "\r\n			" + std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.is_cash) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.can_send_mail_and_personal_shop) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.can_dup) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.unknown) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.block_mail_and_personal_shop) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.is_saleable) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.is_giftable) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stFlagShop.only_display) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.is_new) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.is_hot) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.unknown_bit2) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.unknown_bit3) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.unknown_bit4) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.unknown_bit5) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.unknown_bit6) + "	"
										+ std::to_string((unsigned short)shop.flag_shop.uFlagShop.stIconShop.unknown_bit7)
					+ "\r\n";
#else
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")\r\n";
#endif
			};
		};

		// SetItem IFF
		/*Typeids
		*
		* Ex: 0x2420 20 01 (2420 "NO LONGER SHOWN IN SHOP") (20 "HANA") (01 "ID")
		* 0x(2420~243F)(char_id)(item_id) Character Set "SHOWN IN SHOP"
		* 0x(2440~245F)(char_id)(item_id) Character Set "SHOWN IN SHOP"
		* 0x(24C0~24DE)(char_id)(item_id) Character Set "NOT SHOWN IN SHOP"
		*
		* 0x(2480~249F) ClubSet
		* 0x(24A0~24BF) Ball
		* 0x(24DFC0~24DFDF) Item Active
		* 0x(24DFE0~24DFFF) Item Passive
		*
		* 0x(24DF00~24DFBF) Caddie
		* 0x(24E0~24FF) Caddie
		*
		* 0x(2500~251F) Card and Card Pack
		* 0x(2520~253F) Ring
		*
		*/
		struct SetItem : public Base {
			enum SUB_TYPE : unsigned char {
				COMMON,					// By TH S4 - (COMMON)
				CHARACTER,				// By TH S4 - (CHARACTER) CHARACTER_SET
				PARTS,					// By TH S4 - (PARTS) CHARACTER_SET_NEW
				CLUBSET = 4,			// By TH S4 - (CLUBSET)
				BALL,					// By TH S4 - (BALL)
				ITEM,					// By TH S4 - (ITEM) CHARACTER_SET_DUP_AND_ITEM_PASSIVE_AND_ACTIVE
				CADDIE,					// By TH S4 - (CADDIE)
				CARD,					// By TH S4 - (CARD)
				AUXPART,				// Ring
			};
			// By S4 TH
			enum SUB_TYPE_CHAR : unsigned char {
				NURI,
				HANA,
				AZER,
				CECILIA,
				MAX,
				KOOH,
				ARIN,
				KAZ,
				LUCIA,
				NELL,
				SPIKA,
				NURI_R,
				HANA_R,
				AZER_R,
				CECILIA_R,
				STC_AUXPART = 0xFB,
				STC_CLUBSET,
				STC_CARD,
				EQUIP_ITEM,
				NOEQUIP_ITEM,
			};
			void clear() { memset(this, 0, sizeof(SetItem)); };
			struct Package {
				unsigned int  qntd;
				unsigned int  item_typeid[10];
				unsigned short item_qntd[10];
			};
			Package package;
			unsigned short c[5];			// By TH S4 - (COM[5])
			unsigned short point;			// may be memory alignment
		};

		// Mascot IFF
		struct Mascot : public Base {
			void clear() { memset(this, 0, sizeof(Mascot)); };
			char mpet[40];
			char texture[40];
			unsigned char price[5];
			unsigned char c[5];
			struct Efeito {
				short power_drive;			// power without losing ctrl
				short drop_rate;			// % drop item
				short power_gauge;			// (I think) rate or unit value
				short pang_rate;			// % pang
				short exp_rate;				// % exp
				unsigned char item_slot;
			};
			struct Mensagem {
				unsigned char active;
				short flag;					// may be 1 flag of 2 bytes or 2 flags of 1 byte
				unsigned int  change_price;
			};
			struct BonusPang {
				unsigned short hitting;	// Hitting pang
				unsigned short missing;	// Missing pang
			};
			Efeito efeito;
			Mensagem msg;
			BonusPang bonus_pang;
		};

		// AuxPart IFF
		struct AuxPart : public Base {
			void clear() { memset(this, 0, sizeof(AuxPart)); };
			unsigned short cc[5];	// [0] qty, (I think) used for time and such, 1day, 7, 15, 30, 365 and so on
			unsigned char c[5];
			unsigned char slot[5];
			struct stEfeito {
				unsigned short power_drive;	// power without penalty
				unsigned short drop_rate;
				unsigned short power_gauge;	// Rate or unit value
				unsigned short pang_rate;
				unsigned short exp_rate;
				unsigned short link_power_drive; // In USA/KR it matches the description of the scarlet ring of +2 if equipped with the Crinsom Ring
				std::string toString() {
					return "Efeito [\n\tPOWER_DRIVE: " + std::to_string(power_drive) 
						+ ";\n\tDROP_RATE: " + std::to_string(drop_rate)
						+ ";\n\tPOWER_GAUGE: " + std::to_string(power_gauge)
						+ ";\n\tPANG_RATE: " + std::to_string(pang_rate)
						+ ";\n\tEXP_RATE: " + std::to_string(exp_rate)
						+ ";\n\tLINK_POWER_DRIVE: " + std::to_string(link_power_drive)
						+".\n\t]";
				};
			};
			stEfeito efeito;
			uint32_t link_item_typeid;	// In USA/KR it matches the description of the scarlet ring of +2 if equipped with the Crinsom Ring
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nCC[]: [CC0=" + std::to_string(cc[0]) + ", CC1=" + std::to_string(cc[1]) + ", CC2=" + std::to_string(cc[2]) 
							+ ", CC3=" + std::to_string(cc[3]) + ", CC4=" + std::to_string(cc[4]) + "]"
					+ "\r\nC[]: [C0=" + std::to_string((unsigned short)c[0]) + ", C1=" + std::to_string((unsigned short)c[1]) + ", C2=" 
							+ std::to_string((unsigned short)c[2]) + ", C3=" + std::to_string((unsigned short)c[3]) + ", C4=" + std::to_string((unsigned short)c[4]) + "]"
					+ "\r\nSlot[]: [Slot0=" + std::to_string((unsigned short)slot[0]) + ", Slot1=" + std::to_string((unsigned short)slot[1]) + ", Slot2="
							+ std::to_string((unsigned short)slot[2]) + ", Slot3=" + std::to_string((unsigned short)slot[3]) + ", Slot4=" + std::to_string((unsigned short)slot[4]) + "]"
					+ "\r\n" + efeito.toString()
					+ "\r\nlink_item_typeid: " + std::to_string(link_item_typeid) + "\r\n";
#else
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")";
#endif // _DEBUG
			};
		};

		// Ball IFF
		struct Ball : public Base {
			enum TYPE_BALL_CONSUMABLE : uint32_t {
				CONSUMABLE,
				NO_CONSUMABLE
			};
			void clear() { memset(this, 0, sizeof(Ball)); };
			TYPE_BALL_CONSUMABLE consumable_type;
			char mpet[40];
			unsigned int  bound;			// By TH S4 - (Bound)
			unsigned int  roll;				// By TH S4 - (Roll)
			char fx[7][40];					// By TH S4 - (Fx)
			char fxBone[7][40];				// By TH S4 - (FxBone)
			unsigned short c[5];
			unsigned short point;
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nconsumable type: " + std::to_string((uint32_t)consumable_type)
					+ "\r\nC[]: [C0=" + std::to_string((unsigned short)c[0]) + ", C1=" + std::to_string((unsigned short)c[1]) + ", C2="
							+ std::to_string((unsigned short)c[2]) + ", C3=" + std::to_string((unsigned short)c[3]) + ", C4=" + std::to_string((unsigned short)c[4]) + "]"
					+ "\r\nPoint: " + std::to_string(point) + "\r\n";
#else
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")";
#endif // _DEBUG
			};
		};

		// Caddie IFF
		struct Caddie : public Base {
			void clear() { memset(this, 0, sizeof(Caddie)); };
			unsigned int  valor_mensal;
			char mpet[40];
			unsigned short c[5];
			unsigned short cad_voice_tbl_id;
		};

		// CaddieItem IFF
		struct CaddieItem : public Base {
			enum Type : unsigned char {
				COOKIE,		// CASH
				PANG,		// PANG
				ESPECIAL,	// I THINK so, because there is no item with this one, at least I haven't seen one
				UPGRADE
			};
			void clear() { memset(this, 0, sizeof(CaddieItem)); };
			char faceTex[40];						// By TH S4 - FaceTex[40]
			char bodyTex[40];						// By TH S4 - BodyTex[40]
			unsigned short price[5];				// By TH S4 - (COM[5]) - price[0..3] == shop price from 1 day to 30 days (1 month), price[4] == unit start power gauge that starts the game
			unsigned short point;	
		};

		// CadieMagicBox IFF
		struct CadieMagicBox {
			void clear() { memset(this, 0, sizeof(CadieMagicBox)); };
			unsigned int  seq;
			unsigned int  active;
			unsigned int  setor;
			unsigned int  character;
			unsigned int  level;
			unsigned int  ulUnknown;
			struct ItemReceive {
				unsigned int  _typeid;
				unsigned int  qntd;
			};
			struct ItemTrade {
				unsigned int  _typeid[4];
				unsigned int  qntd[4];
			};
			ItemReceive item_receive;
			ItemTrade item_trade;
			unsigned int  box_random_id;
			char name[40];
			SYSTEMTIME date[2];					// Date[0] Start, Date[1] End
		};

		// CadieMagicBoxRandom IFF
		struct CadieMagicBoxRandom {
			void clear() { memset(this, 0, sizeof(CadieMagicBoxRandom)); };
			unsigned int  id;
			struct ItemRandom {
				unsigned int  _typeid;
				unsigned int  qntd;
				unsigned int  rate;
			};
			ItemRandom item_random;
		};

		// Card IFF
		struct Card : public Base {
			void clear() { memset(this, 0, sizeof(Card)); };
			enum CARD_SUB_TYPE : unsigned {
				T_CHARACTER,
				T_CADDIE,
				T_SPECIAL,
				T_PACK,
				T_BOX_PACK,
				T_NPC,
			};
			unsigned char tipo;
			// it was 40, but I think this 1 below is the \0 value (end of the string)
			char img[41];				// By TH S4 - (Image)
			unsigned short c[5];
			struct Efeito {
				unsigned short type;
				unsigned short qntd;
			};
			Efeito efeito;
			char subIcon[40];			// By TH S4 - SubIcon[40]
			char slotImg[40];			// By TH S4 - SlotImg[40]
			char buffImg[40];			// By TH S4 - BuffImg[40]
			unsigned short tempo;		// By TH S4 - (UseTime) The special Cards that have time
			unsigned short volume;		// By TH S4 - (Volume) Book
			unsigned int  position;		// By TH S4 - (CardIndex) Slot
			unsigned int  flag1;		// !@flag that stores some values of N, R, SR, SC, etc
			unsigned int  flag2;		// flag that stores some values of N, R, SR, SC, etc
			std::string toString() {
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nImg: " + img
					+ "\r\nTipo: " + std::to_string((unsigned short)tipo)
					+ "\r\nEfeito[Type=" + std::to_string(efeito.type) + ", Qntd=" + std::to_string(efeito.qntd) + "]"
					+ "\r\nTempo: " + std::to_string(tempo)
					+ "\r\nVolume(Pack): " + std::to_string(volume)
					+ "\r\nPosistion(Book): " + std::to_string(position)
					+ "\r\nC[C0=" + std::to_string(c[0]) + ", C1=" + std::to_string(c[1]) + ", C2=" + std::to_string(c[2]) + ", C3=" + std::to_string(c[3]) + ", C4=" + std::to_string(c[4]) + "]"
					+ "\r\nFlag[F1=" + std::to_string(flag1) + ", F2=" + std::to_string(flag2) + "]\r\n";
			};
		};

		// Character IFF
		struct Character : public Base {
			void clear() { memset(this, 0, sizeof(Character)); };
			char mpet[40];
			char hairTex[40];					// By TH S4 - HairTex[40]
			char shirtsTex[40];					// By TH S4 - ShirtsTex[40]
			char faceTex[40];					// By TH S4 - FaceTex[40]
			unsigned short c[5];
			unsigned char num_parts;			// By TH S4 - (nParts)
			unsigned char num_accessorios;		// By TH S4 - (nAcsries)
			unsigned int  club_type;			// By TH S4 - (ClubType)
			float scale_club_set;				// By TH S4 - (ClubScale)
			unsigned char c_stat[5];			// By TH S4 - (PCL)
			char mtn_tourney_winner[43];		// By TH S4 - (Mtn30sWinner == Motion Tourney Winner)
		};

		// CharacterMastery IFF
		struct CharacterMastery {
			void clear() { memset(this, 0, sizeof(CharacterMastery)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  seq;
			unsigned int  stats;
			unsigned int  level;
			struct Condition {
				unsigned int  condition[5];
				unsigned int  qntd[5];
			};
			Condition condition;
		};

		// Club IFF
		struct Club : public Base {
			void clear() { memset(this, 0, sizeof(Club)); };
			char mpet[40];
			unsigned short tipo;		// 0 Wood, 1 Iron, 2 SW, 3 PUTT
			unsigned short c[5];
		};

		// ClubSet IFF
		struct ClubSet : public Base {
			void clear() { memset(this, 0, sizeof(ClubSet)); };
			unsigned int  club[4];
			unsigned short c[5];
			unsigned short slot[5];
			struct WorkShop {
				int tipo;							// -1 cannot up rank nor level, 0 can do everything
				unsigned int  rank_s_stat;			// for the rank S bonus stat
				unsigned int  total_recovery;		// recovery points
				float mastery_rate;					// Rate that it will get per hole played
				unsigned int  tipo_rank_s;			// power, spin, control and special for EXP
				unsigned int  can_transform;		// That can Transform into the special clubs
				// the one above may be short and here below have another flag
			};
			WorkShop work_shop;
			unsigned int  ulUnknown;		// May be from the WorkShop, but I do not know yet
			unsigned int  text_pangya;		// [text_pangya_Logo] is the text that appears when hitting pangya, I use it as ClubSet Original Transform because all will have the same logo, and the logo number is the one of the Original
		};

		// ClubSetWorkShopLevelUpLimit IFF
		struct ClubSetWorkShopLevelUpLimit {
			void clear() { memset(this, 0, sizeof(ClubSetWorkShopLevelUpLimit)); };
			unsigned int  tipo;
			unsigned int  rank;
			unsigned short c[5];
			unsigned short point;			// may be memory alignment
		};

		// ClubSetWorkShopLevelUpProb IFF
		struct ClubSetWorkShopLevelUpProb {
			void clear() { memset(this, 0, sizeof(ClubSetWorkShopLevelUpProb)); };
			unsigned int  tipo;
			unsigned int  c[5];
		};

		// ClubSetWorkShopRankUpExp IFF
		struct ClubSetWorkShopRankUpExp {
			void clear() { memset(this, 0, sizeof(ClubSetWorkShopRankUpExp)); };
			unsigned int  tipo;
			unsigned int  rank[6];	// F ~ A
		};

		// Course IFF
		struct Course : public Base {
			enum class eDIFFICULTY_TYPE : uint8_t {
				NONE_STAR,
				ONE_STAR,
				TWO_STAR,
				THREE_STAR,
				FOUR_STAR,
				FIVE_STAR
			};
			enum class eDIFFICULTY_FLAG_TYPE : uint8_t {
				NORMAL,
				EASY,
				HARD
			};
			void clear() { memset(this, 0, sizeof(Course)); };
			char mpet[40];
			char amb_sound[40];									// By TH S4 - (AmbiendSnd)
			struct Star {
				union {
					unsigned char ucStar;
					struct StarMask {
						unsigned char star_num : 4;			// By TH S4 - (Difficulty)
						unsigned char star_difficulty : 4;	// By TH S4 - (DiffFlag)
					};
					StarMask star_mask;
				} uStarMask;
			};
			Star star;
			char xml[43];									// By TH S4 - (TexProp)
			float rate_pang;								// By TH S4 - (Slope)
			char seq[40];									// By TH S4 - (SkyFx)
			struct BonusPangScore {
				uint32_t hio;
				uint32_t albatross;
				uint32_t eagle;
				uint32_t birdie;
				uint32_t par;
				uint32_t overpar;
			};
			BonusPangScore bonus_pang_score_normal;
			BonusPangScore bonus_pang_score_natural;
			struct ParScore {
				unsigned char par_hole[18];
				unsigned char min_score_hole[18];
				unsigned char max_score_hole[18];
			};
			ParScore par_score_hole;
			unsigned short usUnknown; // may be the point
		};

		// CutinInfomation IFF
		struct CutinInfomation {
			// union Condition
			union uCondition {
				uCondition(uint32_t _ul = 0u) : ulCondition(_ul) {};
				void clear() { ulCondition = 0u; };
				uint32_t ulCondition;
				struct {
					uint32_t power_shot : 1;			// By TH S4
					uint32_t double_power_short : 1;	// By TH S4
					uint32_t power_shot_failed : 1;		// By TH S4
					uint32_t chipin : 1, : 0;			// By TH S4
				}stBit;
			};
			void clear() { memset(this, 0, sizeof(CutinInfomation)); };
			unsigned int  active;
			unsigned int  _typeid;						// By TH S4 - (NormalTypeid)
			unsigned int  rare_typeid;					// By TH S4 - (RareTypeid)
			unsigned int  rarity;						// By TH S4 - (Rarity)
			uCondition	  tipo;							// By TH S4 - (Condition) 1= 1PS, 2= 2PS, 4= Pangya Error with special (Toma, Spike Cobra)
			unsigned int  sector;						// By TH S4 - (Img_Pos)
			unsigned int  character_id;					// By TH S4 - (CharIndex) 0 Nuri, 1 Hana, 2 Azer e etc
			struct Img {
				char sprite[40];						// By TH S4 - (Tex)
				unsigned int  tipo;						// By TH S4 - (Ani)
			};
			Img img[4];									// By TH S4 - (Char, Bg, Patter, Text)
			unsigned int  tempo;						// By TH S4 - (Out_Ani)
		};

		// Enchant IFF
		struct Enchant {
			void clear() { memset(this, 0, sizeof(Enchant)); };
			unsigned int  active;
			unsigned int  _typeid;
			uint64_t pang;	// Value to be discounted
		};

		// Furniture IFF
		struct Furniture : public Base {
			void clear() { memset(this, 0, sizeof(Furniture)); };
			char mpet[40];
			unsigned short num;				// By TH S4 - (Num)
			unsigned short is_own;			// By TH S4 - (IsOwn)
			unsigned short is_move;			// By TH S4 - (IsMove) 5 Poster
			unsigned short is_function;		// By TH S4 - (IsFunction)  0x62 Poster B, 0x63 Poster A
			unsigned int  etc;				// By TH S4 - (Etc)
			struct Location {
				float x;
				float y;
				float z;
				float r;
			};
			Location location;
			char texture[3][40];			// By TH S4 - (Tex)
			char texture_org[3][40];		// By TH S4 - (OrgTex)
			unsigned short c[5];			// By TH S4 - (COM[5])
			unsigned short use_time;		// By TH S4 - (UseTime)
		};

		// HairStyle IFF
		struct HairStyle : public Base {
			void clear() { memset(this, 0, sizeof(HairStyle)); };
			unsigned char cor;				// By TH S4 - (HairID)
			unsigned char character;		// By TH S4 - (CharID)
			uint16_t point;					// may be memory alignment
		};

		// Match IFF
		struct Match {
			void clear() { memset(this, 0, sizeof(Match)); };
			unsigned int  active;
			unsigned int  _typeid;
			char name[80];
			unsigned char level;			// By TH S4 - (Level) I do not know what it is, but in all of them it is 10 (0x0A)
			char trophy[6][40];				// By TH S4 - (icon[6][40])
			unsigned char ucUnknown2[3];
		};

		// Skin IFF
		struct Skin : public Base {
			void clear() { memset(this, 0, sizeof(Skin)); };
			char mpet[40];
			unsigned char horizontal_scroll;	// By TH S4 - (HScroll)
			unsigned char vertical_scroll;		// By TH S4 - (VScroll) 256 vertical scroll effect
			unsigned short price[5];
		};

		// Ability IFF
		struct Ability {
			enum class eEFFECT_TYPE : unsigned int  {
				NONE, 
				PIXEL,							// Pixel the value in rate
				PIXEL_BY_WIND_NO_ITEM,			// Pixel depending on the wind the value in rate, if an item or ps is used it cancels the effect
				PIXEL_OVER_WIND_NO_ITEM,		// Pixel above a certain wind the value in rate, if an item or ps is used it cancels the effect
				PIXEL_BY_WIND,					// Pixel depending on the wind the value in rate
				PIXEL_2,						// Pixel the value in rate
				PIXEL_WITH_WEAK_WIND,			// Pixel when the wind is weak the value in rate
				POWER_GAUGE_TO_START_HOLE,		// Power Gauge at the start of the hole for each hole the value in rate
				POWER_GAUGE_MORE_ONE,			// Power Gauge gives one extra bar + 33 Units, the value in rate
				POWER_GUAGE_TO_START_GAME,		// Power Gauge at the start of the game the value in rate
				PAWS_NOT_ACCUMULATE,			// Patinha does not stack with another patinha effect, probability is in rate
				SWITCH_TWO_EFFECT,				// Item with 2 non-simultaneous effects, which effect is in rate, 0 Yards, 1 Power Gauge
				EARCUFF_DIRECTION_WIND,			// Changes the wind direction, the probability is chosen by pangya
				COMBINE_ITEM_EFFECT,			// Item combination, in rate there is the combination ID in (IFF)SetEffectTable
				SAFETY_CLIENT_RANDOM,			// Safety the probability is decided by the client
				PIXEL_RANDOM,					// Random Pixel the value is in rate, the probability is decided by the client
				WIND_1M_RANDOM,					// Random Wind 1m the probability is in rate
				PIXEL_BY_WIND_MIDDLE_DOUBLE,	// Pixel depending on the wind, medium wind gives double, the value in rate
				GROUND_100_PERCENT_RONDOM,		// 100% random Terrain, the probability is in rate
				ASSIST_MIRACLE_SIGN,			// Magic Eye Assist
				VECTOR_SIGN,					// Shows an arrow on the ball, depending on the wind, like the assist trajectory
				ASSIST_TRAJECTORY_SHOT,			// Assist Trajectory Shot
				PAWS_ACCUMULATE,				// Patinha stacks with another patinha effect, the probability is in rate
				POWER_GAUGE_FREE,				// Power Gauge, gains 1 free Power Gauge to use on the shot
				SAFETY_RANDOM,					// Random Safety the probability is in rate
				ONE_IN_ALL_STATS,				// [UNKNOWN] but I will leave the (Combine Itens) ONE IN ALL STATS, gives 1 to all stats, power, cltr, accuracy, spin and curve
				POWER_GAUGE_BY_MISS_SHOT,		// Power Gauge even if you miss pangya or use a Power Gauge item it still gives Power Gauge
				PIXEL_BY_WIND_2,				// Pixel depending on the wind the value is in rate
				PIXEL_WITH_RAIN,				// Pixel when it is raining (recovery) the value is in rate
				NO_RAIN_EFFECT,					// No effect from rain on the terrain
				PUTT_MORE_10Y_RANDOM,			// Random +10y on Putt the probability is in rate
				UNKNOWN_31,
				MIRACLE_SIGN_RANDOM,			// Random Magic Eye the probability is in rate
				UNKNOWN_33,
				DECREASE_1M_OF_WIND,			// Decreases the wind by 1m
			};
			void clear() { memset(this, 0, sizeof(Ability)); };
			unsigned int  _typeid;
			struct Efeito {
				unsigned int  EfeitoOrNo[3];	// Activation at the time of the shot (I THINK)
				unsigned int  type[3];
				float rate[3];
			};
			Efeito efeito;
			SYSTEMTIME date[2];					// [0] - Start, [1] - End
			unsigned int  type;
			unsigned int  type_on;
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nEfeitoOuNao: " + std::to_string(efeito.EfeitoOrNo[0])
					+ ", " + std::to_string(efeito.EfeitoOrNo[1])
					+ ", " + std::to_string(efeito.EfeitoOrNo[2])
					+ "\r\nEfeito:      " + std::to_string(efeito.type[0])
					+ ", " + std::to_string(efeito.type[1])
					+ ", " + std::to_string(efeito.type[2])
					+ "\r\nRate:        " + std::to_string(efeito.rate[0])
					+ ", " + std::to_string(efeito.rate[1])
					+ ", " + std::to_string(efeito.rate[2])
					+ "\r\nDate Start: " + _formatDate(date[0])
					+ "\r\nDate End: " + _formatDate(date[1])
					+ "\r\nType: " + std::to_string(type)
					+ "\tType_On: " + std::to_string(type_on);
#else
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")";
#endif
			};
		};

		// Desc IFF
		struct Desc {
			void clear() { memset(this, 0, sizeof(Desc)); };
			unsigned int  _typeid;
			char description[512];
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nDescription:\r\n" + description + "\r\n";
#else
				return "Typeid: " + std::to_string(_typeid)
					+ "(0x" + hex_util::ltoaToHex(_typeid) + ")";
#endif // _DEBUG
			};
		};

		// GrandPrixAIOptionalData IFF(.sff)
		struct GrandPrixAIOptionalData {
			void clear() { memset(this, 0, sizeof(GrandPrixAIOptionalData)); };
			unsigned int  active;
			unsigned int  id;
			char name[36];
			unsigned int  BetterOrNo;		// The first of the class
			unsigned int  char_id;			// Character ID, 4000000 | ID
			unsigned int  _class;			// GrandPrix class
			unsigned int  parts_typeid[24];
			unsigned char ucUnknown[452];
			std::string toString() {
#ifdef _DEBUG
				return "Info Bot\r\nName: " + std::string(name)
					+ "\r\nID: " + std::to_string(id)
					+ "\r\nCharacter ID: " + std::to_string(char_id)
					+ "\r\nFirster Class: " + std::to_string(BetterOrNo)
					+ "\r\nClass: " + std::to_string(_class)
					+ hex_util::BufferToHexString(ucUnknown, sizeof(ucUnknown));
#else
				return "Info Bot\r\nTypeid: " + std::to_string(id);
#endif
			};
		};

		// GrandPrixConditionEquip IFF
		struct GrandPrixConditionEquip {
			void clear() { memset(this, 0, sizeof(GrandPrixConditionEquip)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  item_typeid;
			char info[512];
			uint32_t ulUnknown;
		};

		// GrandPrixData IFF
		// Typeid 0x[00(1)][00(2)][00(3)][00(4)]
		//
		// (1) 2 bit == 3 Event, 2 bit == 0 Normal
		// (2) Class "Aba"
		// (3) Type (course or different config)
		// (4) Id (sequence) schedules
		struct GrandPrixData {
		public:
			enum GP_ABA : unsigned char {
				ROOKIE,			// Tutorial plays in an instance - Tuto Tab
				BEGINNER,		// Class 1 to 2				 - Tab 1
				JUNIOR,			// Class 3 to 5				 - Tab 2
				SENIOR,			// Class 6 to 9				 - Tab 3
			};

		public:
			void clear() { memset(this, 0, sizeof(GrandPrixData)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  typeid_link;			// Link to other support GrandPrix IFFs
			unsigned int  type;
			unsigned short time_hole;
			char name[64];
			unsigned short usUnknown;			// This may be a continuation of the name, or an unknown value
			struct Ticket {
				unsigned int  _typeid;
				unsigned int  qntd;
			};
			Ticket ticket;
			char img[41];
			struct Flag {
				unsigned char natural;
				unsigned char short_game;
				unsigned char hole_cup_x2;
			};
			Flag flag;
			unsigned int  rule;					// Typeid of the rule, GrandPrix rule Ex: 1m, 10s, 15s, etc
			struct CourseInfo {
				unsigned int  course;
				unsigned int  modo;
				unsigned char qntd_hole;
			};
			CourseInfo course_info;
			unsigned char level_min;
			unsigned char level_max;
			unsigned char ucUnknown;
			unsigned int  condition[2];			// Condition[0] = AVG menor, Condition[1] = AVG maior
			struct BOT {
				int score_max;
				int score_med;
				int score_min;
			};
			BOT bot;
			unsigned int  _class;				// it was difficulty in the old one, but I think it is class
			unsigned int  pang;					// May be the pangs that the bot earns
			struct Reward {
				unsigned int  _typeid[5];
				unsigned int  qntd[5];
				unsigned int  time[5];			// Item time if it is time-limited
			};
			Reward reward;
			SYSTEMTIME open;					// here is when the gp room opens
			SYSTEMTIME start;					// here when it starts
			SYSTEMTIME end;						// here when it ends
			unsigned int  ulUnknown;			// I do not know exactly what it is, but it may be another of the clear_GP
			unsigned int  clear_gp_typeid;		// typeid of the GrandPrix that blocks it if not completed
			unsigned int  lock_yn;				// This one only blocks if there is a typeid of a gp in the one above that is not completed
			char info[512];
			uint32_t ulUnknown2;
		};

		// GrandPrixRankReward IFF
		struct GrandPrixRankReward {
			void clear() { memset(this, 0, sizeof(GrandPrixRankReward)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  rank;
			struct Reward {
				unsigned int  _typeid[5];
				unsigned int  qntd[5];
				unsigned int  time[5];	// Item time if it is time-limited
			};
			Reward reward;
			unsigned int  trophy_typeid;
		};

		// GrandPrixSpecialHole IFF
		struct GrandPrixSpecialHole {
			void clear() { memset(this, 0, sizeof(GrandPrixSpecialHole)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  seq;				// This is the order of the holes, from 1 to 18 depending on the number of holes
			unsigned int  course;
			unsigned int  hole;
		};

		// MemorialShopCoinItem IFF(.sff)
		struct MemorialShopCoinItem {
			enum TYPE_FILTER : unsigned int  {
				SPRING = 1,
				SUMMER,
				FALL,
				WINTER,
				CLUBSET,
				SETITEM,
				EAR,
				WING,
				LUVA,
				RING_R,
				RING_L,
				CADDIE,
				MASCOT,
				SUMMER_HOLYDAY,
				XMAS,
				HALLOWEEN,
				MAN,
				WOMAN,
				NURI,
				HANA,
				AZER,
				CECI,
				MAX,
				KOOH,
				ARIN,
				KAZ,
				LUCIA,
				NELL,
				SPIKA,
				NURI_R,
				HANA_R,
				AZER_R,
				CECI_R,
			};
			void clear() { memset(this, 0, sizeof(MemorialShopCoinItem)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  type;
			unsigned int  probability;
			struct GachaRange {
				unsigned int  number_min;
				unsigned int  number_max;
				bool empty() {
					return (number_min == 0 && number_max == 0);
				};
				bool isBetweenGacha(unsigned int  _number) {
					return (number_min <= _number && _number <= number_max);
				};
			};
			GachaRange gacha_range;
			/*
			+ Filter types
			+ 1, 2, 3 and 4, Season, Spring, Summer, Autumn, Winter
			+ 5 Clubset, 6 SetItem, 7 Ear, 8 Wing, 9 Glove
			+ 10 Right Hand Ring, 11 Left Hand Ring, 12 Caddie
			+ 13 Mascot, 14 Summer (summer variants), 15 Christmas, 16 Halloween
			+ 17 Man, 18 Woman, 19 Nuri, 20 Hana, 21 Azer, 22 Cecilia
			+ 23 Max, 24 Kooh, 25 Arin, 26 Kaz, 27 Lucia, 28 Nell, 29 Spika
			+ 30 Nuri R, 31 Hana R, (32 is Azer R, but ntreev did not make it), 33 Cecilia R
			*/
			unsigned int  filter[10];		// Filters with the item types to search in the other IFF of the memorial shop rare item 
			bool hasFilter(unsigned int  _filter) {
				if (_filter == 0)
					return false;

				for (auto i = 0u; i < (sizeof(filter) / sizeof(filter[0])); ++i)
					if (filter[i] == _filter)
						return true;
				
				return false;
			};
			bool emptyFilter() {
				unsigned int  count = 0u;

				for (auto i = 0u; i < (sizeof(filter) / sizeof(filter[0])); ++i)
					count += filter[i];

				return count == 0;
			};
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nType: " + std::to_string(type)
					+ "\r\nProbability: " + std::to_string(probability)
					+ "\r\nFilter(s): " + std::to_string(filter[0])
					+ ", " + std::to_string(filter[1])
					+ ", " + std::to_string(filter[2])
					+ ", " + std::to_string(filter[3])
					+ ", " + std::to_string(filter[4])
					+ ", " + std::to_string(filter[5])
					+ ", " + std::to_string(filter[6])
					+ ", " + std::to_string(filter[7])
					+ ", " + std::to_string(filter[8])
					+ ", " + std::to_string(filter[9]) + "\r\n";
#else
				return "Typeid: " + std::to_string(_typeid);
#endif
			};
		};

		// MemorialShopRareItem IFF
		struct MemorialShopRareItem {
			void clear() { memset(this, 0, sizeof(MemorialShopRareItem)); };
			unsigned int  active;
			struct Gacha {
				unsigned int  number;
				unsigned int  count;
			};
			Gacha gacha;
			unsigned int  _typeid;
			unsigned int  probability;
			unsigned int  rare_type;		// Rare Type, EX: -1 - 0 normal, 1 - 2 rare, 3 - 4 Super rare
			/*
			+ Filter types
			+ 1, 2, 3 and 4, Season, Spring, Summer, Autumn, Winter
			+ 5 Clubset, 6 SetItem, 7 Ear, 8 Wing, 9 Glove
			+ 10 Right Hand Ring, 11 Left Hand Ring, 12 Caddie
			+ 13 Mascot, 14 Summer (summer variants), 15 Christmas, 16 Halloween
			+ 17 Man, 18 Woman, 19 Nuri, 20 Hana, 21 Azer, 22 Cecilia
			+ 23 Max, 24 Kooh, 25 Arin, 26 Kaz, 27 Lucia, 28 Nell, 29 Spika
			+ 30 Nuri R, 31 Hana R, (32 is Azer R, but ntreev did not make it), 33 Cecilia R
			*/
			unsigned int  filter[10];		// Same as above
			char s_string[28];
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nRare Type: " + std::to_string(rare_type)
					+ "\r\nProbability: " + std::to_string(probability)
					+ "\r\nFilter(s): " + std::to_string(filter[0])
						+ ", " + std::to_string(filter[1])
						+ ", " + std::to_string(filter[2])
						+ ", " + std::to_string(filter[3])
						+ ", " + std::to_string(filter[4])
						+ ", " + std::to_string(filter[5])
						+ ", " + std::to_string(filter[6])
						+ ", " + std::to_string(filter[7])
						+ ", " + std::to_string(filter[8])
						+ ", " + std::to_string(filter[9])
					+ "\r\nString S (Index): " + std::string(s_string) + "\r\n";
#else
				return "Typeid: " + std::to_string(_typeid);
#endif
			};
		};

		// AddonPart IFF
		struct AddonPart {
			void clear() { memset(this, 0, sizeof(AddonPart)); };
			unsigned int  active;
			unsigned int  _typeid;
			char mpet[40];
			char texture[3][40];		// Texture
			char texture_org[3][40];	// Original Texture
		};

		// ArtifactManaInfo IFF
		struct ArtifactManaInfo {
			void clear() { memset(this, 0, sizeof(ArtifactManaInfo)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  mana_typeid;
			char info[132];
			unsigned int  type;			// 0 = All modes (VS, CAMP, MATCH ...) except GrandPrix, 1 = 18H only, 3 = GrandPrix only
			unsigned int  ulUnknown;
			std::string toString() {
#ifdef _DEBUG
				return "Artifact Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nMana Typeid: " + std::to_string(mana_typeid) + "(0x" + hex_util::ltoaToHex(mana_typeid) + ")"
					+ "\r\nType: " + std::to_string(type)
					+ "\r\nulUnknown: " + std::to_string(ulUnknown)
					+ "\r\nInfo.\r\n" + std::string(info);
#else
				return "Artifact Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nMana Typeid: " + std::to_string(mana_typeid) + "(0x" + hex_util::ltoaToHex(mana_typeid) + ")";
#endif
			}
		};

		// CaddieVoiceTable IFF
		struct CaddieVoiceTable {
			void clear() { memset(this, 0, sizeof(CaddieVoiceTable)); };
			unsigned int  _typeid;
			char name[64];
			unsigned char level;
			char shot_name[64];
			uint8_t ucUnknown;
			std::string toString() {
#ifdef _DEBUG
				return "Name: " + std::string(name)
					+ "\r\nTypeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nLevel: " + std::to_string((uint16_t)level)
					+ "\r\nShot Name: " + std::string(shot_name)
					+ "\r\nucUnknown: " + std::to_string((uint16_t)ucUnknown);
#else
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")";
#endif
			};
		};

		// ErrorCodeInfo IFF
		struct ErrorCodeInfo {
			void clear() { memset(this, 0, sizeof(ErrorCodeInfo)); };
			unsigned int  active;
			uint32_t code;
			uint32_t type;
			char info[260];
			std::string toString() {
#ifdef _DEBUG
				return "ErrorCode: " + std::to_string(code) + " (0x" + hex_util::ltoaToHex(code) + ")"
					+ "\r\nType: " + std::to_string(type) + " (0x" + hex_util::ltoaToHex(type) + ")"
					+ "\r\nInfo.\r\n" + std::string(info);
#else
				return "ErrorCode: " + std::to_string(code) + " (0x" + hex_util::lltoaToHex(code) + ")";
#endif
			};
		};

		// FurnitureAbility IFF
		struct FurnitureAbility {
			// union uAbilityType
			union uAbilityType {
				uAbilityType(uint32_t _ul = 0u) : ulAbilityType(_ul) {};
				void clear() { ulAbilityType = 0u; };
				uint32_t ulAbilityType;
				struct {
					uint32_t buff : 1, : 0;			// By TH S4
				}stBit;
			};
			union uSuccessType {
				uSuccessType(uint16_t _ul = 0u) : ulSuccessType(_ul) {};
				void clear() { ulSuccessType = 0u; };
				uint16_t ulSuccessType;
				struct {
					uint16_t stay : 1;			// By TH S4
					uint16_t putin : 1;			// By TH S4
					uint16_t putout : 1, : 0;	// By TH S4
				}stBit;
			};
			union uEffectType {
				uEffectType(uint16_t _ul = 0u) : ulEffectType(_ul) {};
				void clear() { ulEffectType = 0u; };
				uint16_t ulEffectType;
				struct {
					uint16_t me : 1;			// By TH S4
					uint16_t _friend : 1;		// By TH S4
					uint16_t guild : 1;			// By TH S4
					uint16_t all : 1, : 0;		// By TH S4
				}stBit;
			};
			void clear() { memset(this, 0, sizeof(FurnitureAbility)); };
			unsigned int  active;
			unsigned int  _typeid;
			uAbilityType  type;					// By TH S4 - (btAbilityType)
			unsigned int  stay_time;			// By TH S4 - (iStayTime)
			uSuccessType success_type;			// By TH S4 - (btSuccessType)
			uEffectType  effect_type;			// By TH S4 - (btEffectType) 0 Your Furniture, 3 Someone else's Furniture
			unsigned int  set_in_typeid;		// By TH S4 - (dwSetInTID)
			unsigned int  max_qntd;				// By TH S4 - (iMaxCountAtFurniture) The maximum number of items that can be won
			SYSTEMTIME    date;					// Start Date
			unsigned int  during_time;			// By TH S4 - (dwDuringTime)
			struct Item {
				unsigned int  _typeid;
				unsigned int  probability;
			};
			Item item;
			unsigned int  max_count_by_user;	// By TH S4 - (iMaxCountAtUser)
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nType: " + std::to_string(type.ulAbilityType)
					+ "\r\nSetInTypeid): " + std::to_string(set_in_typeid)
					+ "\r\nMax Qntd: " + std::to_string(max_qntd)
					+ "\r\nSuccessType: " + std::to_string(success_type.ulSuccessType)
					+ "\r\nEffectType: " + std::to_string(effect_type.ulEffectType)
					+ "\r\nMaxCountByUser: " + std::to_string(max_count_by_user)
					+ "\r\nStayTime: " + std::to_string(stay_time)
					+ "\r\nDuringTime " + std::to_string(during_time)
					+ "\r\nDate Start: " + _formatDate(date);
#else
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")";
#endif
			};
		};

		// HoleCupDropItem IFF
		struct HoleCupDropItem {
			void clear() { memset(this, 0, sizeof(HoleCupDropItem)); };
			unsigned int  _typeid;
			char animation[40];
		};

		// LevelUpPrizeItem IFF
		struct LevelUpPrizeItem {
			void clear() { memset(this, 0, sizeof(LevelUpPrizeItem)); };
			unsigned char active;
			char name[33];
			unsigned short level;
			struct Reward {
				unsigned int  _typeid[2];
				unsigned int  qntd[2];
				unsigned int  time[2];		// Item time if it is time-limited
			};
			Reward reward;
			char description[132];
		};

		// NonVisibleItemTable IFF
		struct NonVisibleItemTable {
			void clear() { memset(this, 0, sizeof(NonVisibleItemTable)); };
			unsigned int  active;
			unsigned int  type;
			unsigned int  _typeid;
			struct Date {
				SYSTEMTIME start;
				SYSTEMTIME end;
			};
			Date date;	// I THINK
		};

		// PointShop IFF(Old Tiki item exchange system)
		struct PointShop {
			void clear() { memset(this, 0, sizeof(PointShop)); };
			unsigned int  active;
			unsigned int  _typeid;
			unsigned int  point;		// Amount of points that will be exchanged for the items
			unsigned int  qntd;			// Amount of items that will be exchanged for the points
			unsigned int  rarity;		// rarity
		};

		// ScratchRewardSetting IFF (Found in KR)
		struct ScratchRewardSetting {
			struct ItemCounter {
				uint32_t _typeid;
				uint32_t qntd;
			};
			void clear() { memset(this, 0, sizeof(ScratchRewardSetting)); };
			uint32_t active;
			uint32_t id;
			ItemCounter item_counter;
			uint32_t item_zero_counter;
		};

		// SetEffectTable IFF
		/*
		+ Effects
		+ 1 - Cutin
		+ 2 - Unknown
		+ 3 - Unknown
		+ 4 - +1px
		+ 5 - +Yard, the value is in effect_add_power
		+ 6 - +1 all stats
		+ 7 - (-1) wind speed
		+ 8 - Patinha
		*/
		struct SetEffectTable {
			enum eEFFECT : unsigned char {
				ANIMATION = 1,
				UNKNOWN_V2,
				CUTIN,
				PIXEL,
				BASE,			// The 5 - this one is a base value, used when you want to give power without ps and when you want to combine items  only Hana R clothes
				ONE_ALL_STATS,
				WIND_DECREASE,
				PATINHA,
			};
			enum eEFFECT_TYPE : unsigned char {
				UNKNOWN_V1 = 1,
				GAME = 2,
				ROOM = 4,
				LOUNGE = 8,
			};
			void clear() { memset(this, 0, sizeof(SetEffectTable)); };
			unsigned int  id;
			struct Effect {
				unsigned int  effect[3];	// eEFFECT = Effect[0~2] is the one in the description above
				unsigned int  type[3];		// eEFFECT_TYPE = type[0~2], 2 Game, 4 Room e 8 Lounge
			};
			Effect effect;
			struct Item {
				unsigned int  _typeid[5];
				unsigned char active[5];
			};
			Item item;
			unsigned char ucUnknown[11];
			unsigned short slot[5];
			unsigned short effect_add_power;	// Power without penalty
			std::string toString() {
#ifdef _DEBUG
				return "ID: " + std::to_string(id)
					+ "\r\nType(s): " + std::to_string(effect.type[0])
						+ ", " + std::to_string(effect.type[1])
						+ ", " + std::to_string(effect.type[2])
					+ "\r\nEffect(s): " + std::to_string(effect.effect[0])
						+ ", " + std::to_string(effect.effect[1])
						+ ", " + std::to_string(effect.effect[2])
					+ "\r\nItem(ns) Typeid: " + std::to_string(item._typeid[0])
						+ ", " + std::to_string(item._typeid[1])
						+ ", " + std::to_string(item._typeid[2])
						+ ", " + std::to_string(item._typeid[3])
						+ ", " + std::to_string(item._typeid[4])
					+ "\r\nItem(ns) active: " + std::to_string(item.active[0])
						+ ", " + std::to_string(item.active[1])
						+ ", " + std::to_string(item.active[2])
						+ ", " + std::to_string(item.active[3])
						+ ", " + std::to_string(item.active[4])
					+ "\r\nSlot(s): " + std::to_string(slot[0])
						+ ", " + std::to_string(slot[1])
						+ ", " + std::to_string(slot[2])
						+ ", " + std::to_string(slot[3])
						+ ", " + std::to_string(slot[4])
					+ "\r\nEffect Add Power: " + std::to_string(effect_add_power)
					+ "\r\nucUnknown\r\n" + hex_util::BufferToHexString(ucUnknown, sizeof(ucUnknown));
#else
				return "ID: " + std::to_string(id);
#endif
			};
		};

		// ShopLimitItem IFF
		struct ShopLimitItem {
			void clear() { memset(this, 0, sizeof(ShopLimitItem)); };
			unsigned int  active;
			unsigned int  type;
			unsigned int  _typeid;
			uint32_t purchases_without_wait; // Total purchases without wait time (wait_time_hours == 0)
			uint32_t purchases_with_wait;    // Total purchases with wait time
			uint32_t wait_time_hours;        // Wait time in hours until the next purchase
			uint32_t max_repeat_cycles;      // Total number of times the cycle repeats
			struct Date {
				SYSTEMTIME start;
				SYSTEMTIME end;
			};
			Date date;
		};

		// SpecialPrizeItem IFF
		struct SpecialPrizeItem {
			void clear() { memset(this, 0, sizeof(SpecialPrizeItem)); };
			unsigned int  _typeid;
			unsigned int  type;
			float rate;				// Not yet
			std::string toString() {
#ifdef _DEBUG
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")"
					+ "\r\nType: " + std::to_string(type)
					+ "\r\nRate: " + std::to_string(rate);
#else
				return "Typeid: " + std::to_string(_typeid) + "(0x" + hex_util::ltoaToHex(_typeid) + ")";
#endif
			};
		};

		// SubscriptionItemTable IFF
		struct SubscriptionItemTable {
			void clear() { memset(this, 0, sizeof(SubscriptionItemTable)); };
			unsigned int  active;
			unsigned int  type;
			unsigned int  _typeid;
			struct Date {
				SYSTEMTIME start;
				SYSTEMTIME end;
			};
			Date date;	// I THINK
		};

		// TikiShopBase support structure of the (Old Tiki item exchange system)
		struct TikiShopBase {
			void clear() { memset(this, 0, sizeof(TikiShopBase)); };
			unsigned int  id;
			unsigned char type;
			char name[35];
		};

		// TikiPointTable IFF(Old Tiki item exchange system)
		struct TikiPointTable : public TikiShopBase {
			void clear() { memset(this, 0, sizeof(TikiPointTable)); };
			unsigned int  min;
			unsigned int  max;
		};

		// TikieRecipe IFF(Old Tiki item exchange system)
		struct TikiRecipe : public TikiShopBase {
			void clear() { memset(this, 0, sizeof(TikiRecipe)); };
			unsigned int  recipe_qntd[3];
		};

		// TikiSpecialTable IFF(Old Tiki item exchange system)
		struct TikiSpecialTable : public TikiShopBase {
			void clear() { memset(this, 0, sizeof(TikiSpecialTable)); };
			unsigned int  qntd;				// number of recipes, max 4;
			unsigned int  recipe_qntd[4];
		};

		// TimeLimitItem IFF
		struct TimeLimitItem {
			void clear() { memset(this, 0, sizeof(TimeLimitItem)); };
			unsigned int  active;
			unsigned int  _typeid;
			char name[40];
			char icon[40];
			unsigned int  type;
			unsigned int  percent;	// Rate
			unsigned int  time;
		};

		// TwinsItemTable IFF
		struct TwinsItemTable {
			void clear() { memset(this, 0, sizeof(TwinsItemTable)); };
			unsigned int  active;
			unsigned int  type;
			unsigned int  _typeid[5];
		};

    }
}


#if defined(__linux__)
#pragma pack()
#endif

#endif // !_STDA_IFF_DATA_IFF_H
