// Included in the native adapter: native packets never cross the network.
namespace {
struct PendingQuestReward {
    SharedQuestReward reward;
    std::set<u16> existing,source;
    u32 money=0;
};
std::deque<PendingQuestReward> quest_rewards;
PendingQuestReward* active_reward=nullptr;
std::set<std::uint64_t> quest_reward_ids;
struct NativeAward {std::uint64_t id=0; std::vector<u16> items; bool money=false;};
std::map<u16,NativeAward> native_awards;
}
bool reward_actor_pending(std::uint16_t object) {return native_awards.count(object)!=0;}
SharedQuestRewardScope::SharedQuestRewardScope(const char* quest,std::uint64_t received,bool completed) {
    if(!completed || !quest || !quest[0] || active_reward || !collect_world_objects || world_level_is_replica() || !g_actor || !ai().get_alife()) return;
    // Quest instance, rather than snapshot revision, owns the reward.
    std::uint64_t id=1469598103934665603ull;
    for(const unsigned char* c=reinterpret_cast<const unsigned char*>(quest);*c;++c) {id^=*c;id*=1099511628211ull;}
    id^=received; id*=1099511628211ull; if(!id) id=1;
    if(quest_reward_ids.count(id)) return;
    PendingQuestReward pending; pending.reward.id=id; pending.reward.quest=quest; pending.money=g_actor->get_money();
    if(!capture_player_progress(g_actor->ID(),pending.reward.before)) return;
    for(auto object:guest_actor_objects()) {
        GuestInventoryState state;
        const auto character=engine_coopnet::guest_character_identity(object);
        if(character && capture_guest_inventory(object,state)) pending.reward.characters.push_back(character);
    }
    for(const auto& entry:ai().alife().objects().objects()) if(entry.second->ID_Parent==g_actor->ID() && smart_cast<CSE_ALifeInventoryItem*>(entry.second)) pending.existing.insert(entry.first);
    for(auto* item:g_actor->inventory().m_all) pending.existing.insert(item->object().ID());
    quest_reward_ids.insert(id);
    quest_rewards.push_back(std::move(pending)); active_reward=&quest_rewards.back(); tracked=true;
}
SharedQuestRewardScope::~SharedQuestRewardScope() {
    if(!tracked || !active_reward) return;
    auto& pending=*active_reward;
    pending.reward.money=g_actor->get_money()>pending.money ? g_actor->get_money()-pending.money : 0;
    capture_player_progress(g_actor->ID(),pending.reward.after);
    for(const auto& entry:ai().alife().objects().objects()) if(entry.second->ID_Parent==g_actor->ID() && smart_cast<CSE_ALifeInventoryItem*>(entry.second) && !pending.existing.count(entry.first)) pending.source.insert(entry.first);
    for(auto* item:g_actor->inventory().m_all) if(!pending.existing.count(item->object().ID())) pending.source.insert(item->object().ID());
    Msg("* CoopNet quest reward captured: %s id %llu money %u items %u guests %u",pending.reward.quest.c_str(),pending.reward.id,pending.reward.money,static_cast<unsigned>(pending.source.size()),static_cast<unsigned>(pending.reward.characters.size()));
    active_reward=nullptr;
}
void record_quest_reward_item(std::uint16_t item,std::uint16_t recipient) {
    if(active_reward && g_actor && recipient==g_actor->ID() && !active_reward->existing.count(item)) active_reward->source.insert(item);
}
bool take_shared_quest_reward(SharedQuestReward& output) {
    if(quest_rewards.empty() || active_reward || !g_actor || !g_pGameLevel) return false;
    auto& pending=quest_rewards.front(); std::vector<GuestInventoryItem> items;
    for(auto id:pending.source) {
        auto* object=smart_cast<CGameObject*>(Level().Objects.net_Find(id));
        auto* item=object ? smart_cast<CInventoryItem*>(object) : nullptr;
        auto* server=Level().Server->ID_to_entity(id);
        if(!item || !server || object->getDestroy() || object->H_Parent()!=g_actor || server->ID_Parent!=g_actor->ID()) return false;
        NET_Packet update; update.B.count=0; update.r_pos=0; object->net_Export(update); server->UPDATE_Read(update);
        NET_Packet saved; saved.B.count=0; saved.r_pos=0; object->net_Save(saved); server->load(saved);
        auto* inventory=smart_cast<CSE_ALifeInventoryItem*>(server); if(!inventory) return false; inventory->m_fCondition=item->GetCondition();
        NET_Packet spawn; server->Spawn_Write(spawn,TRUE);
        GuestInventoryItem saved_item; saved_item.section=object->cNameSect().c_str();
        saved_item.spawn.assign(spawn.B.data,spawn.B.data+spawn.B.count); items.push_back(std::move(saved_item));
    }
    output=std::move(pending.reward); output.items=std::move(items); quest_rewards.pop_front(); return true;
}
bool grant_shared_quest_reward(std::uint16_t object,const SharedQuestReward& reward,const GuestInventoryState& state) {
    LocalActorPose pose; if(!capture_guest_actor(object,pose)) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    auto& award=native_awards[object];
    if(award.id && award.id!=reward.id) return false;
    award.id=reward.id;
    if(!award.money) {
        actor->set_money(state.money,false);
        if(!apply_player_progress(actor,state.progress)) return false;
        award.money=true;
    }
    while(award.items.size()<reward.items.size()) {
        if(session_items.size()>=768) return false;
        const auto& item=reward.items[award.items.size()];
        auto* abstract=F_entity_Create(item.section.c_str());
        if(!abstract || !smart_cast<CSE_ALifeInventoryItem*>(abstract)) {if(abstract) F_entity_Destroy(abstract);return false;}
        NET_Packet packet; packet.B.count=static_cast<u32>(item.spawn.size()); packet.r_pos=0; memcpy(packet.B.data,item.spawn.data(),item.spawn.size());
        if(!abstract->Spawn_Read(packet)) {F_entity_Destroy(abstract);return false;}
        abstract->ID=abstract->ID_Phantom=0xffff; abstract->ID_Parent=object; abstract->m_bALifeControl=false; abstract->o_Position=actor->Position();
        if(auto* life=smart_cast<CSE_ALifeObject*>(abstract)) {
            life->m_tNodeID=actor->ai_location().level_vertex_id();
            if(auto* parent=smart_cast<CSE_ALifeObject*>(Level().Server->ID_to_entity(object))) life->m_tGraphID=parent->m_tGraphID;
        }
        abstract->Spawn_Write(packet,TRUE); u16 type; packet.r_begin(type);
        auto* created=Level().Server->Process_spawn(packet,Level().Server->GetServerClient()->ID,FALSE,nullptr,true); F_entity_Destroy(abstract);
        if(!created) return false;
        session_items.emplace(created->ID,SessionItem{++item_incarnation,false,true}); award.items.push_back(created->ID);
    }
    for(auto id:award.items) {
        auto* item=smart_cast<CGameObject*>(Level().Objects.net_Find(id));
        if(!item || item->H_Parent()!=actor) return false;
    }
    native_awards.erase(object);
    Msg("* CoopNet shared quest reward applied: object %u quest %s id %llu money %u items %u reputation %d",object,reward.quest.c_str(),reward.id,reward.money,static_cast<unsigned>(reward.items.size()),actor->Reputation());
    return true;
}
void clear_shared_quest_rewards() {quest_rewards.clear();native_awards.clear();quest_reward_ids.clear();active_reward=nullptr;}

void forget_native_awards(std::uint16_t object) {native_awards.erase(object);}
void forget_all_native_awards() {native_awards.clear();}
