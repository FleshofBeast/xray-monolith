void exercise_guest_features_probe(double elapsed) {
    if(!strstr(Core.Params,"-coop_guest_features_probe") || !g_actor || !g_pGameLevel || !Level().bReady) return;
    static unsigned stage=0; static double wait=0;
    static u32 money=0; static std::size_t items=0;
    if(world_level_is_replica()) {
        auto* task=Level().GameTaskManager().HasGameTask(shared_str("coopnet_guest_features_probe"),false);
        const auto goodwill=RELATION_REGISTRY().GetCommunityGoodwill(CHARACTER_COMMUNITY::GetById("stalker")->index,g_actor->ID());
        bool radio=false;
        if(g_actor->game_news_registry) for(const auto& news:g_actor->game_news_registry->registry().objects())
            if(news.news_text=="Guest radio delivery test") radio=true;
        if(!stage && radio && g_actor->get_money()==1500 && g_actor->inventory().m_all.size()==16 && task && task->GetTaskState()==eTaskStateCompleted && g_actor->Reputation()==395 && g_actor->Rank()==1294 && goodwill==859) {
            luabind::functor<bool> check; if(!ai().script_engine().functor("coopnet_guest_ui.probe_hud",check) || !check())
                throw std::runtime_error("Guest stock HUD missing after replicated quest completion");
            Msg("* CoopNet guest features probe: HUD quest completed reputation 395 rank 1294 goodwill 859 rubles %u items %u",g_actor->get_money(),static_cast<unsigned>(g_actor->inventory().m_all.size()));
            stage=1;
        }
        return;
    }
    if(!stage) {
        for(auto object:guest_actor_objects()) {
            GuestInventoryState state;
            if(capture_guest_inventory(object,state) && state.progress.reputation==345 && state.progress.rank==1234) {
                money=state.money;items=state.items.size();
                auto* task=xr_new<CGameTask>(); task->m_ID="coopnet_guest_features_probe"; task->m_Title="CoopNet native quest test";
                task->m_Description="Replicated objective"; task->SetType_script(eTaskTypeAdditional);
                task->AddOnCompleteFunc_script("coopnet_guest_ui.probe_reward");task->CommitScriptHelperContents();
                Level().GameTaskManager().GiveGameTaskToActor(task,0,true,0);Level().GameTaskManager().SetActiveTask(task);
                GAME_NEWS_DATA news; news.news_caption="CoopNet probe";news.news_text="Guest radio delivery test"; news.texture_name="ui_iconsTotal_grouping";news.show_time=5000;
                g_actor->AddGameNews(news);stage=1;wait=0;
                Msg("* CoopNet guest features probe: imported reputation 345 rank 1234 rubles %u items %u",money,static_cast<unsigned>(items));break;
            }
        }
    } else if(stage==1 && (wait+=elapsed)>8) {
        auto* task=Level().GameTaskManager().HasGameTask(shared_str("coopnet_guest_features_probe"),false);
        task->m_Description="Updated objective"; task->SetTaskState(eTaskStateCompleted);stage=2;
    } else if(stage==2) {
        for(auto object:guest_actor_objects()) {
            GuestInventoryState state;
            if(capture_guest_inventory(object,state) && state.progress.reputation==395 && state.progress.rank==1294 && state.money==money+500 && state.items.size()==items+1) {
                Msg("* CoopNet host guest features probe: shared money item reputation rank verified");stage=3;break;
            }
        }
    }
}
