#pragma once
#include "ActorPresence.h"
#include <set>
namespace coopnet {
// Host-authored presentation, independent of movement and inventory ownership.
struct ActorAppearance {
    Identity entity=0;
    std::uint32_t generation=0,level=0;
    std::string body,weapon,left_bone,right_bone,one_hand_bone;
    std::array<float,6> offset{}; // native weapon offset: position, HPB
    std::uint8_t animation=0;
    bool one_hand=false;
    std::vector<std::string> hidden_body,hidden_weapon;
};
inline bool valid_appearance(const ActorAppearance& a) {
    if (!a.entity || !a.generation || !a.level || a.body.empty() || !valid_actor_visual(a.body) ||
        !valid_actor_visual(a.weapon) || a.animation>16 || a.hidden_body.size()>64 || a.hidden_weapon.size()>64) return false;
    for (const auto& name:{a.left_bone,a.right_bone,a.one_hand_bone})
        if (name.size()>63 || !valid_actor_visual(name)) return false;
    if (!a.weapon.empty() && (a.left_bone.empty() || a.right_bone.empty() || a.one_hand_bone.empty() || !a.animation)) return false;
    for (float f:a.offset) if (!std::isfinite(f) || std::abs(f)>10) return false;
    for (const auto* list:{&a.hidden_body,&a.hidden_weapon}) {
        std::set<std::string> names;
        for (const auto& name:*list) if (name.empty() || name.size()>63 || !valid_actor_visual(name) || !names.insert(name).second) return false;
    }
    return true;
}
inline std::vector<std::uint8_t> encode_appearance(const ActorAppearance& a) {
    if (!valid_appearance(a)) throw std::invalid_argument("Invalid actor appearance");
    Writer w; w.integer(a.entity,8); w.integer(a.generation,4); w.integer(a.level,4);
    auto string=[&](const std::string& s){w.integer(s.size(),2); for(unsigned char c:s) w.integer(c,1);};
    for (const auto& s:{a.body,a.weapon,a.left_bone,a.right_bone,a.one_hand_bone}) string(s);
    w.integer(a.animation,1); w.integer(a.one_hand,1);
    for(float f:a.offset){std::uint32_t bits;std::memcpy(&bits,&f,4);w.integer(bits,4);}
    for (const auto* list:{&a.hidden_body,&a.hidden_weapon}) {w.integer(list->size(),1);for(const auto& s:*list) string(s);}
    return w.bytes;
}
inline bool decode_appearance(const std::vector<std::uint8_t>& bytes,ActorAppearance& out) {
    Reader r(bytes); ActorAppearance a; std::uint64_t g,l,n,animation,hand;
    if(!r.integer(a.entity,8)||!r.integer(g,4)||!r.integer(l,4)) return false;
    a.generation=static_cast<std::uint32_t>(g);a.level=static_cast<std::uint32_t>(l);
    auto string=[&](std::string& s,unsigned max){if(!r.integer(n,2)||n>max||n>r.remaining())return false;for(std::uint64_t i=0;i<n;++i){std::uint64_t c;if(!r.integer(c,1))return false;s.push_back(static_cast<char>(c));}return true;};
    if(!string(a.body,191)||!string(a.weapon,191)||!string(a.left_bone,63)||!string(a.right_bone,63)||!string(a.one_hand_bone,63)||
        !r.integer(animation,1)||!r.integer(hand,1)||hand>1) return false;
    a.animation=static_cast<std::uint8_t>(animation);a.one_hand=hand!=0;
    for(float& f:a.offset){std::uint64_t bits;if(!r.integer(bits,4))return false;auto b=static_cast<std::uint32_t>(bits);std::memcpy(&f,&b,4);}
    for(auto* list:{&a.hidden_body,&a.hidden_weapon}) {std::uint64_t count;if(!r.integer(count,1)||count>64)return false;for(std::uint64_t i=0;i<count;++i){std::string s;if(!string(s,63))return false;list->push_back(s);}}
    if(r.remaining()||!valid_appearance(a))return false;out=std::move(a);return true;
}
}
