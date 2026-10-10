#pragma once
#include "EngineActorBridge.h"
#include "SharedWorld.h"
#include "PlayerName.h"
namespace engine_coopnet {
struct CharacterSave {
    std::uint64_t character=0;
    std::string name;
    ActorConditionState condition;
    coopnet::InventoryView inventory;
};
inline std::string character_save_name(const std::string& name) {
    std::string safe;
    for(unsigned char c:name) safe.push_back(c<32 || c==127 || std::string("<>:\"/\\|?*").find(char(c))!=std::string::npos ? '_' : char(c));
    while(!safe.empty() && (safe.back()=='.' || safe.back()==' ')) safe.pop_back();
    if(safe.empty()) safe="Player";
    std::string upper=safe; for(auto& c:upper) if(c>='a' && c<='z') c=char(c-'a'+'A');
    const auto base=upper.substr(0,upper.find('.'));
    if(base=="CON" || base=="PRN" || base=="AUX" || base=="NUL" ||
        (base.size()==4 && (base.substr(0,3)=="COM" || base.substr(0,3)=="LPT") && base[3]>='1' && base[3]<='9')) safe="_"+safe;
    return safe+".coopchar";
}
inline bool valid_character_save(const CharacterSave& s) {
    return s.character && coopnet::valid_player_name(s.name) && coopnet::valid_inventory_view(s.inventory) && s.inventory.npc_disposition.empty() &&
        std::isfinite(s.condition.health) && s.condition.health>=-1 && s.condition.health<=1 &&
        std::isfinite(s.condition.power) && s.condition.power>=-1 && s.condition.power<=1 &&
        std::isfinite(s.condition.radiation) && s.condition.radiation>=0 && s.condition.radiation<=1;
}
inline std::uint64_t character_checksum(const std::vector<std::uint8_t>& bytes,std::size_t length) {
    std::uint64_t hash=1469598103934665603ull;
    for(std::size_t i=0;i<length;++i) {hash^=bytes[i];hash*=1099511628211ull;} return hash;
}
inline std::vector<std::uint8_t> encode_character_save(const CharacterSave& s) {
    if(!valid_character_save(s)) throw std::invalid_argument("Invalid character save");
    coopnet::SharedWriter w; w.integer(0x31534343,4); w.integer(s.character,8); w.string(s.name);
    for(float value:{s.condition.health,s.condition.power,s.condition.radiation}) w.number(value);
    const auto count=(std::max)(std::size_t(1),(s.inventory.items.size()+3)/4); w.integer(count,2);
    for(std::size_t offset=0,part=0;part<count;++part,offset+=4) {
        coopnet::InventoryViewChunk chunk; chunk.view=s.inventory; chunk.view.items.clear();
        chunk.total=static_cast<std::uint16_t>(s.inventory.items.size()); chunk.offset=static_cast<std::uint16_t>(offset);
        const auto end=(std::min)(offset+4,s.inventory.items.size());
        chunk.view.items.insert(chunk.view.items.end(),s.inventory.items.begin()+offset,s.inventory.items.begin()+end);
        const auto data=coopnet::encode_view_chunk(chunk); w.integer(data.size(),4);
        for(auto c:data) w.integer(c,1);
    }
    const auto checksum=character_checksum(w.bytes,w.bytes.size()); w.integer(checksum,8); return w.bytes;
}
inline bool decode_character_save(const std::vector<std::uint8_t>& bytes,CharacterSave& output) {
    if(bytes.size()<40 || bytes.size()>coopnet::shared_limit) return false;
    coopnet::Reader r(bytes); CharacterSave s; std::uint64_t n,count;
    if(!r.integer(n,4) || n!=0x31534343 || !r.integer(s.character,8) || !coopnet::shared_string(r,s.name,64)) return false;
    for(float* value:{&s.condition.health,&s.condition.power,&s.condition.radiation}) if(!coopnet::shared_number(r,*value)) return false;
    if(!r.integer(count,2) || !count || count>64) return false;
    coopnet::InventoryViewAssembly assembly; bool complete=false;
    for(unsigned i=0;i<count;++i) {
        if(complete || !r.integer(n,4) || n>coopnet::max_payload || n>r.remaining()) return false;
        std::vector<std::uint8_t> part; const auto length=n;
        for(unsigned k=0;k<length;++k) {if(!r.integer(n,1)) return false;part.push_back(static_cast<std::uint8_t>(n));}
        coopnet::InventoryViewChunk chunk;
        if(!coopnet::decode_view_chunk(part,chunk) || !assembly.append(chunk,complete,s.inventory)) return false;
    }
    if(!complete || !r.integer(n,8) || n!=character_checksum(bytes,bytes.size()-8) || r.remaining() || !valid_character_save(s)) return false;
    output=std::move(s); return true;
}
}
