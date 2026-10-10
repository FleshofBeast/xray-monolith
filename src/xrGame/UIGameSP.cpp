#include "pch_script.h"
#include "uigamesp.h"
#include "actor.h"
#include "level.h"
#include "../xrEngine/xr_input.h"

#ifdef DEBUG
#include "attachable_item.h"
#endif

#include "game_cl_Single.h"
#include "xr_level_controller.h"
#include "actorcondition.h"
#include "../xrEngine/xr_ioconsole.h"
#include "object_broker.h"
#include "GameTaskManager.h"
#include "GameTask.h"

#include "ui/UIActorMenu.h"
#include "ui/UIPdaWnd.h"
#include "ui/UITalkWnd.h"
#include "ui/UIMessageBox.h"

#include "Inventory.h"
#include "MainMenu.h"
#include "../xrEngine/CoopNetRuntime.h"
#include "ui/UI3tButton.h"
#include "ui/UIStatic.h"
#include "ui/UIWndCallback.h"
#include "ui/UIListBox.h"
#include "ui/UIListBoxItem.h"
#include <dinput.h>

namespace {
class CCoopRespawnDialog : public CUIDialogWnd,public CUIWndCallback {
    CUIListBox* players;
    ui_shader panel_shader;
    CUIStatic* status;
    std::vector<engine_coopnet::SpectatorPlayer> roster;
    void xr_stdcall Select(CUIWindow*,void*) {
        const auto index=players->GetSelectedIDX();
        if (index<roster.size() && roster[index].alive) engine_coopnet::select_spectator(roster[index].entity,roster[index].generation);
    }
    void Cycle(int direction) {
        if (roster.empty()) return;
        int start=-1;
        for (unsigned i=0;i<roster.size();++i) if (roster[i].entity==engine_coopnet::spectator_target()) start=static_cast<int>(i);
        if (start<0) start=direction>0 ? -1 : 0;
        for (unsigned step=1;step<=roster.size();++step) {
            const auto index=static_cast<unsigned>((start+direction*static_cast<int>(step)+static_cast<int>(roster.size())*2)%static_cast<int>(roster.size()));
            if (roster[index].alive && engine_coopnet::select_spectator(roster[index].entity,roster[index].generation)) { players->SetSelectedIDX(index); return; }
        }
    }
public:
    CCoopRespawnDialog() {
        panel_shader->create("hud\\crosshair");
        SetWndPos(Fvector2().set(24,180)); SetWndSize(Fvector2().set(310,390));
        auto* title=xr_new<CUIStatic>(); title->SetAutoDelete(true); AttachChild(title);
        title->SetWndPos(Fvector2().set(18,16)); title->SetWndSize(Fvector2().set(274,32));
        title->TextItemControl()->SetFont(UI().Font().pFontLetterica18Russian); title->TextItemControl()->SetText("Spectate teammates");
        status=xr_new<CUIStatic>(); status->SetAutoDelete(true); AttachChild(status);
        status->SetWndPos(Fvector2().set(18,300)); status->SetWndSize(Fvector2().set(274,80));
        status->TextItemControl()->SetFont(UI().Font().pFontLetterica16Russian); status->TextItemControl()->SetTextComplexMode(true);
        players=xr_new<CUIListBox>(); players->SetAutoDelete(true); AttachChild(players);
        players->SetWndPos(Fvector2().set(18,58)); players->SetWndSize(Fvector2().set(274,226));
        players->SetFont(UI().Font().pFontLetterica16Russian); players->SetTextColor(0xffeeeeee); players->SetItemHeight(28);
        Register(players); AddCallback(players,LIST_ITEM_CLICKED,CUIWndCallback::void_function(this,&CCoopRespawnDialog::Select));
        Show(false);
    }
    void Draw() override {
        Frect panel; GetAbsoluteRect(panel); UI().ClientToScreenScaled(panel.lt); UI().ClientToScreenScaled(panel.rb);
        UIRender->SetShader(*panel_shader);
        UIRender->StartPrimitive(6,IUIRender::ptTriList,IUIRender::pttTL);
        const u32 color=0xd9181d22;
        UIRender->PushPoint(panel.x1,panel.y1,0,color,0,0); UIRender->PushPoint(panel.x2,panel.y1,0,color,0,0); UIRender->PushPoint(panel.x2,panel.y2,0,color,0,0);
        UIRender->PushPoint(panel.x1,panel.y1,0,color,0,0); UIRender->PushPoint(panel.x2,panel.y2,0,color,0,0); UIRender->PushPoint(panel.x1,panel.y2,0,color,0,0);
        UIRender->FlushPrimitive();
        CUIDialogWnd::Draw();
    }
    void SendMessage(CUIWindow* window,s16 message,void* data=nullptr) override { OnEvent(window,message,data); }
    void Update() override {
        auto current=engine_coopnet::spectator_players(); bool changed=current.size()!=roster.size();
        if (!changed) for (unsigned i=0;i<current.size();++i) if (current[i].entity!=roster[i].entity || current[i].generation!=roster[i].generation || current[i].name!=roster[i].name || current[i].alive!=roster[i].alive) changed=true;
        if (changed) {
            roster=std::move(current); players->Clear();
            for (const auto& player:roster) {
                auto* item=players->AddItem(); item->SetWndSize(Fvector2().set(264,28));
                item->SetText(player.name.c_str()); item->SetTextColor(player.alive ? 0xffeeeeee : 0xff707070); item->Enable(player.alive);
            }
        }
        bool valid=false;
        for (unsigned i=0;i<roster.size();++i) if (roster[i].alive && roster[i].entity==engine_coopnet::spectator_target()) { valid=true; players->SetSelectedIDX(i); }
        if (!valid && engine_coopnet::spectator_target()) { engine_coopnet::clear_spectator(); Cycle(1); }
        if (strstr(Core.Params,"-coop_respawn_spectator_probe") && engine_coopnet::player_downed() && !engine_coopnet::spectator_target()) {
            for (unsigned i=0;i<roster.size();++i) if (roster[i].alive) {
                players->SetSelectedIDX(i); Select(nullptr,nullptr);
                OnKeyboardAction(DIK_A,WINDOW_KEY_PRESSED); OnKeyboardAction(DIK_D,WINDOW_KEY_PRESSED);
                Msg("* CoopNet spectator UI probe: player selection and A/D cycle passed"); break;
            }
        }
        const char* text=engine_coopnet::spectator_target() ? "A / D: switch player\nEnter: respawn at this player\nEsc: main menu" : "Click a living player's name to spectate.\nA / D: select player\nWaiting if no teammates are alive.";
        status->TextItemControl()->SetText(text);
        CUIDialogWnd::Update();
    }
    bool OnKeyboardAction(int key,EUIMessages action) override {
        if (action==WINDOW_KEY_PRESSED && key==DIK_ESCAPE) { engine_coopnet::clear_spectator(); HideDialog(); MainMenu()->Activate(true); return true; }
        if (action==WINDOW_KEY_PRESSED && (key==DIK_A || key==DIK_D)) { Cycle(key==DIK_A ? -1 : 1); return true; }
        if (action==WINDOW_KEY_PRESSED && (key==DIK_RETURN || key==DIK_NUMPADENTER)) { if (engine_coopnet::spectator_target()) engine_coopnet::request_respawn(); return true; }
        return CUIDialogWnd::OnKeyboardAction(key,action);
    }
};
}


CUIGameSP::CUIGameSP()
	: m_game(NULL), m_game_objective(NULL)
{
	TalkMenu = xr_new<CUITalkWnd>();
	UIChangeLevelWnd = xr_new<CChangeLevelWnd>();
}

CUIGameSP::~CUIGameSP()
{
    if (CoopRespawnWnd) CoopRespawnWnd->HideDialog();
    delete_data(CoopRespawnWnd);
	delete_data(TalkMenu);
	delete_data(UIChangeLevelWnd);
}

void CUIGameSP::HideShownDialogs()
{
	HideActorMenu();
	//HidePdaMenu();
	CUIDialogWnd* mir = TopInputReceiver();
	if (mir && mir == TalkMenu)
	{
		mir->HideDialog();
	}
}

void CUIGameSP::SetClGame(game_cl_GameState* g)
{
	inherited::SetClGame(g);
	m_game = smart_cast<game_cl_Single*>(g);
	R_ASSERT(m_game);
}
#ifdef DEBUG
	void attach_adjust_mode_keyb(int dik);
	void attach_draw_adjust_mode();
	void hud_adjust_mode_keyb(int dik);
	void hud_draw_adjust_mode();
#endif

void CUIGameSP::OnFrame()
{
	inherited::OnFrame();
    if (engine_coopnet::player_downed() && !MainMenu()->IsActive() && !Device.Paused()) {
        if (!CoopRespawnWnd) CoopRespawnWnd=xr_new<CCoopRespawnDialog>();
        if (!CoopRespawnWnd->IsShown()) {
            HideShownDialogs(); CoopRespawnWnd->ShowDialog(false);
            Msg("* CoopNet Respawn dialog opened");
        }
    } else if (CoopRespawnWnd && CoopRespawnWnd->IsShown()) { CoopRespawnWnd->HideDialog(); engine_coopnet::clear_spectator(); }

	if (Device.Paused()) return;

	if (m_game_objective)
	{
		bool b_remove = false;
		int dik = get_action_dik(kSCORES, 0);
		if (dik && !pInput->iGetAsyncKeyState(dik))
			b_remove = true;

		dik = get_action_dik(kSCORES, 1);
		if (!b_remove && dik && !pInput->iGetAsyncKeyState(dik))
			b_remove = true;

		if (b_remove)
		{
			RemoveCustomStatic("main_task");
			RemoveCustomStatic("secondary_task");
			m_game_objective = NULL;
		}
	}
}

bool CUIGameSP::IR_UIOnKeyboardPress(int dik)
{
	if (inherited::IR_UIOnKeyboardPress(dik)) return true;
	if (Device.Paused()) return false;

#ifdef DEBUG
	hud_adjust_mode_keyb	(dik);
	attach_adjust_mode_keyb	(dik);
#endif

	CInventoryOwner* pInvOwner = smart_cast<CInventoryOwner*>(Level().CurrentEntity());
	if (!pInvOwner) return false;
	CEntityAlive* EA = smart_cast<CEntityAlive*>(Level().CurrentEntity());
	if (!EA || !EA->g_Alive()) return false;

	CActor* pActor = smart_cast<CActor*>(pInvOwner);
	if (!pActor)
		return false;

	if (!pActor->g_Alive())
		return false;

	switch (get_binded_action(dik))
	{
	case kACTIVE_JOBS:
		{
			if (!psActorFlags.test(AF_3D_PDA) && !pActor->inventory_disabled())
			{
				::luabind::functor<bool> funct;
				if (ai().script_engine().functor("pda.pda_use", funct))
				{
					if (funct())
						ShowPdaMenu();
				}
			}
			break;
		}
	case kINVENTORY:
		{
			if (!pActor->inventory_disabled())
			{
				if (psActorFlags.test(AF_3D_PDA) && CurrentGameUI()->GetPdaMenu().IsShown())
					pActor->inventory().Activate(NO_ACTIVE_SLOT);

				ShowActorMenu();
			}
			break;
		}

	case kSCORES:
		if (!pActor->inventory_disabled())
		{
			m_game_objective = AddCustomStatic("main_task", true);
			CGameTask* t1 = Level().GameTaskManager().ActiveTask();
			m_game_objective->m_static->TextItemControl()->SetTextST((t1) ? t1->m_Title.c_str() : "st_no_active_task");

			if (t1 && t1->m_Description.c_str())
			{
				StaticDrawableWrapper* sm2 = AddCustomStatic("secondary_task", true);
				sm2->m_static->TextItemControl()->SetTextST(t1->m_Description.c_str());
			}
		}
		break;
	}

	return false;
}
#ifdef DEBUG
void CUIGameSP::Render()
{
	inherited::Render();
	hud_draw_adjust_mode();
	attach_draw_adjust_mode();
}
#endif


void CUIGameSP::StartTrade(CInventoryOwner* pActorInv, CInventoryOwner* pOtherOwner)
{
	//.	if( MainInputReceiver() )	return;

	//---- before trade mode ---------------------------
	::luabind::functor<bool> funct1;
	if (ai().script_engine().functor("actor_menu_inventory.CUIActorMenu_OnMode_Trade", funct1))
	{
		CGameObject* GO = smart_cast<CGameObject*>(pOtherOwner);
		if (funct1(GO->lua_game_object()))
			return;
	}
	//---------------------------------------------------------
	
	ActorMenu->SetActor(pActorInv);
	ActorMenu->SetPartner(pOtherOwner);

	ActorMenu->SetMenuMode(mmTrade);
	ActorMenu->ShowDialog(true);
}

void CUIGameSP::StartUpgrade(CInventoryOwner* pActorInv, CInventoryOwner* pMech)
{
	//.	if( MainInputReceiver() )	return;
	
	//---- before upgrade mode ---------------------------
	::luabind::functor<bool> funct1;
	if (ai().script_engine().functor("actor_menu_inventory.CUIActorMenu_OnMode_Upgrade", funct1))
	{
		CGameObject* GO = smart_cast<CGameObject*>(pMech);
		if (funct1(GO->lua_game_object()))
			return;
	}
	//---------------------------------------------------------
	
	ActorMenu->SetActor(pActorInv);
	ActorMenu->SetPartner(pMech);

	ActorMenu->SetMenuMode(mmUpgrade);
	ActorMenu->ShowDialog(true);
}

void CUIGameSP::StartTalk(bool disable_break)
{
	RemoveCustomStatic("main_task");
	RemoveCustomStatic("secondary_task");

	TalkMenu->b_disable_break = disable_break;
	TalkMenu->ShowDialog(true);
}


void CUIGameSP::StartCarBody(CInventoryOwner* pActorInv, CInventoryOwner* pOtherOwner) //Deadbody search
{
	if (TopInputReceiver()) return;
	
	//---- before Loot mode ---------------------------
	::luabind::functor<bool> funct1;
	if (ai().script_engine().functor("actor_menu_inventory.CUIActorMenu_OnMode_DeadBodySearch", funct1))
	{
		CGameObject* GO = smart_cast<CGameObject*>(pOtherOwner);
		if (funct1(GO->lua_game_object()))
			return;
	}
	//---------------------------------------------------------
		
	ActorMenu->SetActor(pActorInv);
	ActorMenu->SetPartner(pOtherOwner);

	ActorMenu->SetMenuMode(mmDeadBodySearch);
	ActorMenu->ShowDialog(true);
}

void CUIGameSP::StartCarBody(CInventoryOwner* pActorInv, CInventoryBox* pBox) //Deadbody search
{
	if (TopInputReceiver()) return;
	
	//---- before Loot mode ---------------------------
	::luabind::functor<bool> funct1;
	if (ai().script_engine().functor("actor_menu_inventory.CUIActorMenu_OnMode_DeadBodySearch", funct1))
	{
		CGameObject* GO = smart_cast<CGameObject*>(pBox);
		if (funct1(GO->lua_game_object()))
			return;
	}
	//---------------------------------------------------------
	
	ActorMenu->SetActor(pActorInv);
	ActorMenu->SetInvBox(pBox);
	VERIFY(pBox);

	ActorMenu->SetMenuMode(mmDeadBodySearch);
	ActorMenu->ShowDialog(true);
}


extern ENGINE_API BOOL bShowPauseString;

void CUIGameSP::ChangeLevel(GameGraph::_GRAPH_ID game_vert_id,
                            u32 level_vert_id,
                            Fvector pos,
                            Fvector ang,
                            Fvector pos2,
                            Fvector ang2,
                            bool b_use_position_cancel,
                            const shared_str& message_str,
                            bool b_allow_change_level)
{
	if (TopInputReceiver() != UIChangeLevelWnd)
	{
		UIChangeLevelWnd->m_game_vertex_id = game_vert_id;
		UIChangeLevelWnd->m_level_vertex_id = level_vert_id;
		UIChangeLevelWnd->m_position = pos;
		UIChangeLevelWnd->m_angles = ang;
		UIChangeLevelWnd->m_position_cancel = pos2;
		UIChangeLevelWnd->m_angles_cancel = ang2;
		UIChangeLevelWnd->m_b_position_cancel = b_use_position_cancel;
		UIChangeLevelWnd->m_b_allow_change_level = b_allow_change_level;
		UIChangeLevelWnd->m_message_str = message_str;

		UIChangeLevelWnd->ShowDialog(true);
	}
}

CChangeLevelWnd::CChangeLevelWnd()
{
	m_messageBox = xr_new<CUIMessageBox>();
	m_messageBox->SetAutoDelete(true);
	AttachChild(m_messageBox);
}

void CChangeLevelWnd::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	if (pWnd == m_messageBox)
	{
		if (msg == MESSAGE_BOX_YES_CLICKED)
		{
			OnOk();
		}
		else if (msg == MESSAGE_BOX_NO_CLICKED || msg == MESSAGE_BOX_OK_CLICKED)
		{
			OnCancel();
		}
	}
	else
		inherited::SendMessage(pWnd, msg, pData);
}

void CChangeLevelWnd::OnOk()
{
	HideDialog();
	NET_Packet p;
	p.w_begin(M_CHANGE_LEVEL);
	p.w(&m_game_vertex_id, sizeof(m_game_vertex_id));
	p.w(&m_level_vertex_id, sizeof(m_level_vertex_id));
	p.w_vec3(m_position);
	p.w_vec3(m_angles);

	Level().Send(p, net_flags(TRUE));
}

void CChangeLevelWnd::OnCancel()
{
	HideDialog();
	if (m_b_position_cancel)
		Actor()->MoveActor(m_position_cancel, m_angles_cancel);
}

bool CChangeLevelWnd::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
	if (keyboard_action == WINDOW_KEY_PRESSED)
	{
		if (is_binded(kQUIT, dik))
			OnCancel();
		return true;
	}
	return inherited::OnKeyboardAction(dik, keyboard_action);
}

bool g_block_pause = false;

void CChangeLevelWnd::Show()
{
	m_messageBox->InitMessageBox(m_b_allow_change_level
		                             ? "message_box_change_level"
		                             : "message_box_change_level_disabled");
	SetWndPos(m_messageBox->GetWndPos());
	m_messageBox->SetWndPos(Fvector2().set(0.0f, 0.0f));
	SetWndSize(m_messageBox->GetWndSize());

	m_messageBox->SetText(m_message_str.c_str());


	g_block_pause = true;
	Device.Pause(TRUE, TRUE, TRUE, "CChangeLevelWnd_show");
	bShowPauseString = FALSE;
}

void CChangeLevelWnd::Hide()
{
	g_block_pause = false;
	Device.Pause(FALSE, TRUE, TRUE, "CChangeLevelWnd_hide");
}
