#pragma once

struct STradeItemCostEntry
{
	shared_str	section;
	u32			count = 0;
};

struct STradeBuyRequirements
{
	u32							money_ru = 0;
	xr_vector<STradeItemCostEntry> items;
	bool						has_item_cost = false;

	void Clear()
	{
		money_ru = 0;
		items.clear();
		has_item_cost = false;
	}
};

class CInventoryItem;
class CInventoryOwner;
class CTrade;

class CTradeItemCostParser
{
public:
	static void ParseCommaList(LPCSTR str, xr_vector<STradeItemCostEntry>& out);
	static void AggregateEntries(xr_vector<STradeItemCostEntry>& entries);
};

class CTradeItemCostService
{
public:
	static void ParseCostItemsFromString(LPCSTR str, xr_vector<STradeItemCostEntry>& out);

	static void BuildBuyRequirements(
		CInventoryItem* item,
		CTrade* trade,
		bool buying_from_trader,
		bool trader_uses_alt,
		LPCSTR override_cost_items,
		STradeBuyRequirements& out);

	static void AggregateRequirements(const STradeBuyRequirements& src, STradeBuyRequirements& total);

	static u32 CountItemsInInventory(CInventoryOwner* owner, LPCSTR section);
	static bool CanAfford(CInventoryOwner* actor, const STradeBuyRequirements& req);
	static bool ConsumeItems(CInventoryOwner* actor, const STradeBuyRequirements& req);

	static void FormatRequirementsText(const STradeBuyRequirements& req, LPSTR buf, u32 buf_size);
};
