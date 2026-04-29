#include "StdAfx.h"
#include "UIActorMenuQoL.h"

#include "UIActorMenuBase.h"
#include "UICellItem.h"
#include "UIDragDropListEx.h"

#include "../../xrUI/UICursor.h"
#include "../../xrEngine/xr_input.h"

CUIActorMenuQoL::CUIActorMenuQoL(CUIActorMenuBase* owner) : m_pOwner(owner) {}

void CUIActorMenuQoL::ResetSwipeSelection()
{
	m_bSwipeSelectionActive = false;
	m_swipeSelectedItems.clear();
}

void CUIActorMenuQoL::SwipeTakeItem(CUICellItem* ci)
{
	if (!ci || !m_pOwner || !m_pOwner->GetInventoryOwner() || !(PIItem)ci->m_pData || !ci->OwnerList())
		return;

	while (ci->ChildsCount() > 0 && m_pOwner->ToBag(ci, false))
	{
	}
	m_pOwner->ToBag(ci, false);
}

void CUIActorMenuQoL::SwipePutItem(CUICellItem* ci)
{
	if (!ci || !m_pOwner || !m_pOwner->GetInventoryOwner() || !(PIItem)ci->m_pData || !ci->OwnerList())
		return;

	while (ci->ChildsCount() > 0 && m_pOwner->ToDeadBodyBag(ci, false))
	{
	}
	m_pOwner->ToDeadBodyBag(ci, false);
}

void CUIActorMenuQoL::SwipeTakeItemToTrade(CUICellItem* ci)
{
	if (!ci || !m_pOwner || !m_pOwner->GetInventoryOwner() || !(PIItem)ci->m_pData || !ci->OwnerList())
		return;

	bool moved = false;
	while (ci->ChildsCount() > 0 && m_pOwner->ToActorTrade(ci, false))
	{
		moved = true;
	}

	if (m_pOwner->ToActorTrade(ci, false))
	{
		moved = true;
	}

	if (moved && m_pOwner->GetMenuMode() == mmTrade)
	{
		m_pOwner->UpdatePrices();
	}
}

void CUIActorMenuQoL::SwipePutItemFromTrade(CUICellItem* ci)
{
	if (!ci || !m_pOwner || !m_pOwner->GetInventoryOwner() || !(PIItem)ci->m_pData || !ci->OwnerList())
		return;

	bool moved = false;
	while (ci->ChildsCount() > 0 && m_pOwner->ToBag(ci, false))
	{
		moved = true;
	}

	if (m_pOwner->ToBag(ci, false))
	{
		moved = true;
	}

	if (moved && m_pOwner->GetMenuMode() == mmTrade)
	{
		m_pOwner->UpdatePrices();
	}
}

void CUIActorMenuQoL::MarkStackAsSelected(CUICellItem* ci, u16 item_id)
{
	if (!ci)
		return;

	m_swipeSelectedItems.insert(item_id);
	for (u32 i = 0, count = ci->ChildsCount(); i < count; ++i)
	{
		if (CUICellItem* child = ci->Child(i))
		{
			if (PIItem child_item = (PIItem)child->m_pData)
			{
				m_swipeSelectedItems.insert(child_item->object_id());
			}
		}
	}
}

bool CUIActorMenuQoL::TrySwipeSelection(float x, float y)
{
	if (!m_pOwner || (m_pOwner->GetMenuMode() != mmDeadBodySearch && m_pOwner->GetMenuMode() != mmTrade))
		return false;

	if (!pInput->iGetAsyncKeyState(SDL_SCANCODE_LCTRL) || !pInput->iGetAsyncBtnState(0))
	{
		if (m_bSwipeSelectionActive)
		{
			ResetSwipeSelection();
		}
		return false;
	}

	if (CUIDragDropListEx::m_drag_item != nullptr)
	{
		return false;
	}

	m_bSwipeSelectionActive = true;

	Fvector2 cursor_pos = GetUICursor().GetCursorPosition();
	Frect clientArea;
	Ivector2 cell;
	CUICellItem* ci = nullptr;
	PIItem item = nullptr;
	u16 item_id = 0;

	if (m_pOwner->GetMenuMode() == mmTrade)
	{
		if ((m_pOwner->GetTradePartnerList() && m_pOwner->GetTradePartnerList()->IsShown() &&
			 (m_pOwner->GetTradePartnerList()->GetClientArea(clientArea), clientArea.in(cursor_pos))) ||
			(m_pOwner->GetTradePartnerBagList() && m_pOwner->GetTradePartnerBagList()->IsShown() &&
			 (m_pOwner->GetTradePartnerBagList()->GetClientArea(clientArea), clientArea.in(cursor_pos))))
		{
			return false;
		}

		if (m_pOwner->GetTradeActorBagList() && m_pOwner->GetTradeActorBagList()->IsShown() &&
			m_pOwner->GetTradeActorBagList()->ItemsCount() > 0 &&
			(m_pOwner->GetTradeActorBagList()->GetClientArea(clientArea), clientArea.in(cursor_pos)) &&
			(cell = m_pOwner->GetTradeActorBagList()->PickCell(cursor_pos), cell.x >= 0 && cell.y >= 0) &&
			(ci = m_pOwner->GetTradeActorBagList()->GetCellAt(cell).m_item) != nullptr &&
			(item = (PIItem)ci->m_pData) != nullptr &&
			(item_id = item->object_id(), m_swipeSelectedItems.find(item_id) == m_swipeSelectedItems.end()) &&
			m_pOwner->CanMoveToPartner(item))
		{
			MarkStackAsSelected(ci, item_id);
			SwipeTakeItemToTrade(ci);
			return true;
		}

		if (m_pOwner->GetTradeActorList() && m_pOwner->GetTradeActorList()->IsShown() &&
			m_pOwner->GetTradeActorList()->ItemsCount() > 0 &&
			(m_pOwner->GetTradeActorList()->GetClientArea(clientArea), clientArea.in(cursor_pos)) &&
			(cell = m_pOwner->GetTradeActorList()->PickCell(cursor_pos), cell.x >= 0 && cell.y >= 0) &&
			(ci = m_pOwner->GetTradeActorList()->GetCellAt(cell).m_item) != nullptr &&
			(item = (PIItem)ci->m_pData) != nullptr &&
			(item_id = item->object_id(), m_swipeSelectedItems.find(item_id) == m_swipeSelectedItems.end()))
		{
			MarkStackAsSelected(ci, item_id);
			SwipePutItemFromTrade(ci);
			return true;
		}
	}

	if (m_pOwner->GetPartnerList() && m_pOwner->GetPartnerList()->IsShown() &&
		m_pOwner->GetPartnerList()->ItemsCount() > 0 &&
		(m_pOwner->GetPartnerList()->GetClientArea(clientArea), clientArea.in(cursor_pos)) &&
		(cell = m_pOwner->GetPartnerList()->PickCell(cursor_pos), cell.x >= 0 && cell.y >= 0) &&
		(ci = m_pOwner->GetPartnerList()->GetCellAt(cell).m_item) != nullptr &&
		(item = (PIItem)ci->m_pData) != nullptr &&
		(item_id = item->object_id(), m_swipeSelectedItems.find(item_id) == m_swipeSelectedItems.end()) &&
		m_pOwner->IsAllowTakeFromInvBox(ci))
	{
		MarkStackAsSelected(ci, item_id);
		SwipeTakeItem(ci);
		return true;
	}

	if (m_pOwner->GetMenuMode() == mmDeadBodySearch && m_pOwner->GetActorList() && m_pOwner->GetActorList()->IsShown() &&
		m_pOwner->GetActorList()->ItemsCount() > 0 &&
		(m_pOwner->GetActorList()->GetClientArea(clientArea), clientArea.in(cursor_pos)) &&
		(cell = m_pOwner->GetActorList()->PickCell(cursor_pos), cell.x >= 0 && cell.y >= 0) &&
		(ci = m_pOwner->GetActorList()->GetCellAt(cell).m_item) != nullptr &&
		(item = (PIItem)ci->m_pData) != nullptr &&
		(item_id = item->object_id(), m_swipeSelectedItems.find(item_id) == m_swipeSelectedItems.end()) &&
		m_pOwner->IsAllowPlaceToInvBox(ci))
	{
		MarkStackAsSelected(ci, item_id);
		SwipePutItem(ci);
		return true;
	}

	return false;
}

void CUIActorMenuQoL::MoveWholeStack(CUICellItem* itm, const std::function<bool(CUICellItem*, bool)>& move_func)
{
	if (!itm || !move_func)
		return;

	while (itm->ChildsCount() > 0 && move_func(itm, false))
	{
	}
	move_func(itm, false);
}

bool CUIActorMenuQoL::ProcessShiftDoubleClick(CUICellItem* itm)
{
	if (!itm || !m_pOwner || !pInput->iGetAsyncKeyState(SDL_SCANCODE_LSHIFT))
		return false;

	if (CUIDragDropListEx::m_drag_item != nullptr)
	{
		return false;
	}

	const EDDListType t_old = m_pOwner->GetListType(itm->OwnerList());

	switch (t_old)
	{
	case iActorSlot:
		if (m_pOwner->GetMenuMode() == mmDeadBodySearch && m_pOwner->IsAllowPlaceToInvBox(itm))
		{
			MoveWholeStack(itm, [this](CUICellItem* ci, bool useCursorPos) { return m_pOwner->ToDeadBodyBag(ci, useCursorPos); });
		}
		else if (m_pOwner->IsAllowTakeFromInvBox(itm))
		{
			MoveWholeStack(itm, [this](CUICellItem* ci, bool useCursorPos) { return m_pOwner->ToBag(ci, useCursorPos); });
		}
		return true;

	case iActorBag:
		if (m_pOwner->GetMenuMode() == mmTrade)
		{
			MoveWholeStack(itm, [this](CUICellItem* ci, bool useCursorPos) { return m_pOwner->ToActorTrade(ci, useCursorPos); });
			return true;
		}
		if (m_pOwner->GetMenuMode() == mmDeadBodySearch)
		{
			MoveWholeStack(itm, [this](CUICellItem* ci, bool useCursorPos) { return m_pOwner->ToDeadBodyBag(ci, useCursorPos); });
			return true;
		}
		return false;

	case iActorBelt:
	case iActorTrade:
	case iDeadBodyBag:
		MoveWholeStack(itm, [this](CUICellItem* ci, bool useCursorPos) { return m_pOwner->ToBag(ci, useCursorPos); });
		return true;

	case iPartnerTradeBag:
		MoveWholeStack(itm, [this](CUICellItem* ci, bool useCursorPos) { return m_pOwner->ToPartnerTrade(ci, useCursorPos); });
		return true;

	case iPartnerTrade:
		MoveWholeStack(itm, [this](CUICellItem* ci, bool useCursorPos) { return m_pOwner->ToPartnerTradeBag(ci, useCursorPos); });
		return true;

	default:
		return false;
	}
}

