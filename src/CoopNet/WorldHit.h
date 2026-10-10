#pragma once
#include "WorldState.h"
namespace coopnet {
struct WorldHit {
    Identity actor=0,target=0,incarnation=0;
    std::uint32_t generation=0,level=0,sequence=0,motion_epoch=0;
    std::uint16_t bone=0xffff;
    std::uint8_t type=0;
    bool aim_bullet=false,add_wound=true;
    float power=0,impulse=0,armor_piercing=0;
    std::array<float,3> direction{},bone_position{};
};
inline bool valid_world_hit(const WorldHit& h) {
    if (!h.actor || !h.target || !h.incarnation || !h.generation || !h.level ||
        h.type>10 || !std::isfinite(h.power) || h.power<=0 || h.power>1000 ||
        !std::isfinite(h.impulse) || h.impulse<0 || h.impulse>100000 ||
        !std::isfinite(h.armor_piercing) || h.armor_piercing<0 || h.armor_piercing>1) return false;
    float length=0;
    for (float v:h.direction) { if (!std::isfinite(v) || std::abs(v)>1.01f) return false; length+=v*v; }
    for (float v:h.bone_position) if (!std::isfinite(v) || std::abs(v)>100) return false;
    return length>.9f && length<1.1f;
}
inline std::vector<std::uint8_t> encode_world_hit(const WorldHit& h) {
    if (!valid_world_hit(h)) throw std::invalid_argument("Invalid world hit");
    Writer w; w.integer(h.actor,8); w.integer(h.target,8); w.integer(h.incarnation,8);
    for (auto v:{h.generation,h.level,h.sequence,h.motion_epoch}) w.integer(v,4);
    w.integer(h.bone,2); w.integer(h.type,1);
    w.integer((h.aim_bullet ? 1u : 0u)|(h.add_wound ? 2u : 0u),1);
    for (float v:{h.power,h.impulse,h.armor_piercing}) write_world_float(w,v);
    for (const auto& a:{h.direction,h.bone_position}) for (float v:a) write_world_float(w,v);
    return w.bytes;
}
inline bool decode_world_hit(const std::vector<std::uint8_t>& bytes,WorldHit& output) {
    Reader r(bytes); WorldHit h; std::uint64_t v;
    if (!r.integer(h.actor,8) || !r.integer(h.target,8) || !r.integer(h.incarnation,8)) return false;
    for (auto* field:{&h.generation,&h.level,&h.sequence,&h.motion_epoch}) {
        if (!r.integer(v,4)) return false; *field=static_cast<std::uint32_t>(v);
    }
    if (!r.integer(v,2)) return false; h.bone=static_cast<std::uint16_t>(v);
    if (!r.integer(v,1)) return false; h.type=static_cast<std::uint8_t>(v);
    if (!r.integer(v,1) || v>3) return false; h.aim_bullet=(v&1)!=0; h.add_wound=(v&2)!=0;
    for (auto* field:{&h.power,&h.impulse,&h.armor_piercing}) if (!read_world_float(r,*field)) return false;
    for (auto* a:{&h.direction,&h.bone_position}) for (auto& field:*a) if (!read_world_float(r,field)) return false;
    if (r.remaining() || !valid_world_hit(h)) return false;
    output=h; return true;
}
}
