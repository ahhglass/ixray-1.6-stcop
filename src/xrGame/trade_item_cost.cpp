#include "StdAfx.h"
#include "trade_item_cost.h"
#include "inventory_item.h"
#include "InventoryOwner.h"
#include "Inventory.h"
#include "trade.h"
#include "../../xrEngine/string_table.h"
#include "../xrEngine/xr_object.h"

namespace
{
	void trim_inplace(xr_string& s)
	{
		while (!s.empty() && (s[0] == ' ' || s[0] == '\t'))
			s.erase(0, 1);
		while (!s.empty())
		{
			const char c = s.back();
			if (c == ' ' || c == '\t')
				s.pop_back();
			else
				break;
		}
	}
}

void CTradeItemCostParser::AggregateEntries(xr_vector<STradeItemCostEntry>& entries)
{
	if (entries.empty())
		return;

	xr_vector<STradeItemCostEntry> aggregated;
	aggregated.reserve(entries.size());

	for (const STradeItemCostEntry& entry : entries)
	{
		if (!entry.section.size() || entry.count == 0)
			continue;

		bool found = false;
		for (STradeItemCostEntry& dst : aggregated)
		{
			if (dst.section == entry.section)
			{
				dst.count += entry.count;
				found = true;
				break;
			}
		}

		if (!found)
			aggregated.push_back(entry);
	}

	entries.swap(aggregated);
}

void CTradeItemCostParser::ParseCommaList(LPCSTR str, xr_vector<STradeItemCostEntry>& out)
{
	out.clear();
	if (!str || !str[0])
		return;

	string1024 token = {};
	const int count = _GetItemCount(str);
	for (int i = 0; i < count; ++i)
	{
		_GetItem(str, i, token);
		xr_string section = token;
		trim_inplace(section);
		if (section.empty())
			continue;

		bool found = false;
		for (STradeItemCostEntry& entry : out)
		{
			if (!xr_strcmp(entry.section, section.c_str()))
			{
				++entry.count;
				found = true;
				break;
			}
		}

		if (!found)
		{
			STradeItemCostEntry entry;
			entry.section = section.c_str();
			entry.count = 1;
			out.push_back(entry);
		}
	}

	AggregateEntries(out);
}

void CTradeItemCostService::ParseCostItemsFromString(LPCSTR str, xr_vector<STradeItemCostEntry>& out)
{
	CTradeItemCostParser::ParseCommaList(str, out);
}

void CTradeItemCostService::BuildBuyRequirements(
	CInventoryItem* item,
	CTrade* trade,
	bool buying_from_trader,
	bool trader_uses_alt,
	LPCSTR override_cost_items,
	STradeBuyRequirements& out)
{
	out.Clear();
	if (!item || !trade)
		return;

	xr_vector<STradeItemCostEntry> parsed;
	if (override_cost_items && override_cost_items[0])
	{
		ParseCostItemsFromString(override_cost_items, parsed);
	}
	else if (trader_uses_alt)
	{
		parsed = item->GetCostItems();
	}

	if (!parsed.empty())
	{
		out.items = parsed;
		out.has_item_cost = true;
		// Item payment replaces legacy trade price for this purchase.
		// Optional RU surcharge: cost_ru = N in item section (both items + RU).
		out.money_ru = READ_IF_EXISTS(pSettings, r_u32, item->object().cNameSect(), "cost_ru", 0);
	}
	else
	{
		out.money_ru = trade->GetItemPrice(item, buying_from_trader, false);
	}
}

void CTradeItemCostService::AggregateRequirements(const STradeBuyRequirements& src, STradeBuyRequirements& total)
{
	total.money_ru += src.money_ru;

	if (!src.has_item_cost)
		return;

	total.has_item_cost = true;

	for (const STradeItemCostEntry& entry : src.items)
	{
		bool found = false;
		for (STradeItemCostEntry& dst : total.items)
		{
			if (dst.section == entry.section)
			{
				dst.count += entry.count;
				found = true;
				break;
			}
		}

		if (!found)
			total.items.push_back(entry);
	}
}

u32 CTradeItemCostService::CountItemsInInventory(CInventoryOwner* owner, LPCSTR section)
{
	if (!owner || !section || !section[0])
		return 0;

	u32 count = 0;
	for (PIItem item : owner->inventory().m_ruck)
	{
		if (!xr_strcmp(item->m_section_id, section))
			++count;
	}

	return count;
}

bool CTradeItemCostService::CanAfford(CInventoryOwner* actor, const STradeBuyRequirements& req)
{
	if (!actor)
		return false;

	if (actor->get_money() < req.money_ru)
		return false;

	if (!req.has_item_cost)
		return true;

	for (const STradeItemCostEntry& entry : req.items)
	{
		if (CountItemsInInventory(actor, entry.section.c_str()) < entry.count)
			return false;
	}

	return true;
}

bool CTradeItemCostService::ConsumeItems(CInventoryOwner* actor, const STradeBuyRequirements& req)
{
	if (!actor || !req.has_item_cost)
		return true;

	for (const STradeItemCostEntry& entry : req.items)
	{
		u32 remaining = entry.count;

		while (remaining > 0)
		{
			PIItem found = nullptr;
			for (PIItem item : actor->inventory().m_ruck)
			{
				if (!xr_strcmp(item->m_section_id, entry.section.c_str()))
				{
					found = item;
					break;
				}
			}

			if (!found)
				return false;

			found->SetDropManual(false);
			found->object().DestroyObject();
			--remaining;
		}
	}

	return true;
}

void CTradeItemCostService::FormatRequirementsText(const STradeBuyRequirements& req, LPSTR buf, u32 buf_size)
{
	buf[0] = 0;
	if (buf_size == 0)
		return;

	xr_string result;

	if (req.has_item_cost)
	{
		for (const STradeItemCostEntry& entry : req.items)
		{
			if (!result.empty())
				result += ", ";

			const char* name = entry.section.c_str();
			if (pSettings->section_exist(name) && pSettings->line_exist(name, "inv_name_short"))
				name = pSettings->r_string(name, "inv_name_short");

			const char* translated = g_pStringTable->translate(name).c_str();
			string64 chunk = {};
			xr_sprintf(chunk, "%u x %s", entry.count, translated);
			result += chunk;
		}
	}

	if (req.money_ru > 0)
	{
		if (!result.empty())
			result += " + ";

		string64 money_chunk = {};
		xr_sprintf(money_chunk, "%u RU", req.money_ru);
		result += money_chunk;
	}

	xr_strcpy(buf, buf_size, result.c_str());
}
