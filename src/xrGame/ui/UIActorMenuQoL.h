#pragma once

#include "../../xrCore/xrCore.h"
#include <functional>

class CUIActorMenuBase;
class CUICellItem;

class CUIActorMenuQoL
{
private:
	CUIActorMenuBase* m_pOwner = nullptr;

	bool m_bSwipeSelectionActive = false;
	xr_set<u16> m_swipeSelectedItems;

public:
	explicit CUIActorMenuQoL(CUIActorMenuBase* owner);
	~CUIActorMenuQoL() = default;

	void ResetSwipeSelection();
	bool TrySwipeSelection(float x, float y);
	bool ProcessShiftDoubleClick(CUICellItem* itm);

private:
	void SwipeTakeItem(CUICellItem* ci);
	void SwipePutItem(CUICellItem* ci);
	void SwipeTakeItemToTrade(CUICellItem* ci);
	void SwipePutItemFromTrade(CUICellItem* ci);
	void MarkStackAsSelected(CUICellItem* ci, u16 item_id);
	void MoveWholeStack(CUICellItem* itm, const std::function<bool(CUICellItem*, bool)>& move_func);
};

