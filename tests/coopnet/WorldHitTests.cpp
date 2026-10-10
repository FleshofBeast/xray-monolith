#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
#include <limits>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"World hit test failed at "<<line<<'\n'; std::exit(1); } }
#define require(v) check((v),__LINE__)
int main() {
    WorldHit h; h.actor=20; h.target=300; h.incarnation=400; h.generation=1; h.level=10;
    h.sequence=0xfffffffeu; h.direction={0,0,1}; h.power=.5f; h.type=6; h.aim_bullet=true; h.add_wound=false;
    WorldHit decoded; auto bytes=encode_world_hit(h);
    require(bytes.size()==80 && decode_world_hit(bytes,decoded) && decoded.target==300 && decoded.power==.5f && decoded.aim_bullet && !decoded.add_wound);
    for (std::size_t n=0;n<bytes.size();++n) require(!decode_world_hit({bytes.begin(),bytes.begin()+n},decoded));
    auto malformed=bytes; malformed.push_back(0); require(!decode_world_hit(malformed,decoded));
    malformed=bytes; malformed[43]=4; require(!decode_world_hit(malformed,decoded));
    auto invalid=h; invalid.power=std::numeric_limits<float>::quiet_NaN(); require(!valid_world_hit(invalid));
    invalid=h; invalid.direction={0,0,0}; require(!valid_world_hit(invalid));
    invalid=h; invalid.type=11; require(!valid_world_hit(invalid));
    invalid=h; invalid.incarnation=0; require(!valid_world_hit(invalid));
    invalid=h; invalid.armor_piercing=2; require(!valid_world_hit(invalid));
    require(valid_contract({Message::WorldHit,Channel::Combat,Delivery::ReliableOrdered,0,{}}));
    require(!valid_contract({Message::WorldHit,Channel::Combat,Delivery::UnreliableSequenced,0,{}}));
    HostPump host; ClientPump client; unsigned hits=0;
    host.start(10,1,{1,1},[] { return Identity{100}; });
    auto link=MemoryTransport::pair(); auto* raw=link.first.get();
    require(host.attach(1,std::move(link.second))); client.start(std::move(link.first),2,{1,1});
    for (unsigned i=0;i<5;++i) { client.update(.01); host.update(.01); }
    const auto player=client.session().welcome().player;
    host.set_world_hit_handler([&](Identity sender,const WorldHit& hit) { require(sender==player && hit.target==300); ++hits; });
    require(host.create_actor({20,player,2,1,10})); require(host.create_actor({21,1,1,1,10}));
    require(client.send_world_hit(h)==SendResult::Disconnected);
    require(host.assign_level(player,10,1000)); host.update(0); client.update(0);
    require(client.acknowledge_level(10)); host.update(0); client.update(0);
    require(client.send_world_hit(h)==SendResult::Sent); host.update(0); require(hits==1);
    require(client.send_world_hit(h)==SendResult::Sent); host.update(0); require(hits==1);
    h.sequence=0; require(client.send_world_hit(h)==SendResult::Sent); host.update(0); require(hits==2);
    h.sequence=0xffffffffu; require(client.send_world_hit(h)==SendResult::Sent); host.update(0); require(hits==2);
    h.generation=2; h.sequence=1;
    require(raw->send({Message::WorldHit,Channel::Combat,Delivery::ReliableOrdered,h.sequence,encode_world_hit(h)})==SendResult::Sent);
    host.update(0); require(hits==2);
    h.generation=1;
    for (std::uint32_t sequence=1;sequence<=130;++sequence) {
        h.sequence=sequence; require(client.send_world_hit(h)==SendResult::Sent); host.update(0);
    }
    require(hits==128); // The bounded burst cannot starve the owner thread.
    host.update(1); h.sequence=131;
    require(client.send_world_hit(h)==SendResult::Sent); host.update(0); require(hits==129);
    h.generation=1; h.actor=21; require(client.send_world_hit(h)==SendResult::Invalid);
    require(raw->send({Message::WorldHit,Channel::Combat,Delivery::ReliableOrdered,h.sequence,encode_world_hit(h)})==SendResult::Sent);
    host.update(0); require(hits==129 && host.ready_participants()==1);
    client.stop(); host.stop();
    std::cout<<"World hit codec, bounds, ownership, replay, wrap and generation tests passed\n";
}
