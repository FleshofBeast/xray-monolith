#include "../../src/CoopNet/Radio.h"
#include "../../src/CoopNet/CharacterSave.h"
#include "../../src/CoopNet/QuestRewards.h"
#include <iostream>
#define require(v) do {if(!(v)) {std::cerr<<"Guest features failure "<<__LINE__<<'\n';return 1;}} while(false)
int main() {
    coopnet::PlayerProgress progress; progress.present=true; progress.reputation=-2000; progress.rank=1234;
    progress.factions={{"stalker",-456},{"freedom",789}}; progress.story={{"s123",321}};
    coopnet::Writer w; coopnet::write_progress(w,progress);
    coopnet::Reader r(w.bytes); coopnet::PlayerProgress decoded;
    require(coopnet::read_progress(r,decoded) && !r.remaining() && decoded==progress);
    for(std::size_t n=0;n<w.bytes.size();++n) {
        std::vector<std::uint8_t> short_bytes(w.bytes.begin(),w.bytes.begin()+n);
        coopnet::Reader truncated(short_bytes); decoded.reputation=99;
        require(!coopnet::read_progress(truncated,decoded) && decoded.reputation==99);
    }
    auto invalid=progress; invalid.factions.push_back(invalid.factions.front()); require(!coopnet::valid_progress(invalid));
    std::vector<coopnet::RadioRecord> news={{1,123,5000,0,"Wolf","Test message","ui_inGame2_radio"},{2,124,7000,1,"Quest","Completed",""}},received;
    const auto radio=coopnet::encode_radio(news); require(coopnet::decode_radio(radio,received) && received.size()==2 && received[1].text=="Completed");
    for(std::size_t n=0;n<radio.size();++n) require(!coopnet::decode_radio({radio.begin(),radio.begin()+n},received));
    auto corrupt=radio; corrupt.push_back(0); require(!coopnet::decode_radio(corrupt,received));
    engine_coopnet::GuestInventoryState character; character.money=1000; character.progress=progress;
    engine_coopnet::SharedQuestReward reward; reward.id=123; reward.money=500; reward.before=progress; reward.after=progress;
    reward.after.reputation+=50; reward.after.rank+=60; reward.after.factions[0].second+=70;
    reward.items={{"bandage",{1,2,3}}};
    require(engine_coopnet::journal_quest_reward(character,reward)==engine_coopnet::RewardJournalResult::Applied);
    require(character.money==1500 && character.progress.reputation==-1950 && character.progress.rank==1294 && character.progress.factions[1].second==-386 && character.items.size()==1);
    require(engine_coopnet::journal_quest_reward(character,reward)==engine_coopnet::RewardJournalResult::AlreadyApplied && character.money==1500 && character.items.size()==1);
    reward.id=124; reward.items[0].spawn.clear();
    require(engine_coopnet::journal_quest_reward(character,reward)==engine_coopnet::RewardJournalResult::Invalid && character.money==1500);
    engine_coopnet::GuestSave save; save.scope=1; save.character=2; save.game=3; save.mods=4; save.sequence=5;
    save.condition={1,1,0}; save.inventory=character; save.inventory.personal_goodwill={{std::uint16_t(123),-2000}};
    engine_coopnet::GuestSave restored;
    require(engine_coopnet::decode_guest_save(engine_coopnet::encode_guest_save(save),restored));
    require(restored.inventory.progress==character.progress && restored.inventory.rewards==character.rewards && restored.inventory.personal_goodwill==save.inventory.personal_goodwill);
    engine_coopnet::CharacterSave portable; portable.character=20; portable.name="Test Character";
    portable.condition={.75f,.8f,.1f}; portable.inventory.actor=1; portable.inventory.generation=portable.inventory.level=portable.inventory.revision=1;
    portable.inventory.money=1500; portable.inventory.progress=character.progress;
    const auto packed=engine_coopnet::encode_character_save(portable); engine_coopnet::CharacterSave loaded;
    require(engine_coopnet::decode_character_save(packed,loaded) && loaded.name==portable.name && loaded.inventory.progress==portable.inventory.progress);
    auto damaged=packed; damaged[25]^=1; require(!engine_coopnet::decode_character_save(damaged,loaded));
    for(std::size_t n=0;n<packed.size();++n) require(!engine_coopnet::decode_character_save({packed.begin(),packed.begin()+n},loaded));
    require(engine_coopnet::character_save_name("../CON") == ".._CON.coopchar");
    require(engine_coopnet::character_save_name("CON") == "_CON.coopchar");
    require(engine_coopnet::character_save_name("Test Character") == "Test Character.coopchar");
    std::cout<<"Guest reputation, radio bounds, reward replay and durable progress tests passed\n";
}

