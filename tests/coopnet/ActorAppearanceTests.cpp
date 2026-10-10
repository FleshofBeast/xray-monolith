#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <cstdlib>
#include <iostream>
using namespace coopnet;
void require(bool v){if(!v)std::abort();}
int main(){
    ActorAppearance a;a.entity=90;a.generation=1;a.level=3;a.body="actors/stalker.ogf";
    a.weapon="weapons/ak74.ogf";a.left_bone="left";a.right_bone="right";a.one_hand_bone="finger";
    a.animation=2;a.hidden_weapon={"silencer","scope"};a.offset={.1f,.2f,.3f,0,0,0};
    auto bytes=encode_appearance(a);ActorAppearance decoded;
    require(decode_appearance(bytes,decoded)&&encode_appearance(decoded)==bytes);
    for(std::size_t n=0;n<bytes.size();++n)require(!decode_appearance({bytes.begin(),bytes.begin()+n},decoded));
    auto invalid=bytes;invalid.push_back(0);require(!decode_appearance(invalid,decoded));
    auto bad=a;bad.weapon="../outside.ogf";require(!valid_appearance(bad));
    bad=a;bad.hidden_weapon.push_back("scope");require(!valid_appearance(bad));
    bad=a;bad.offset[0]=std::numeric_limits<float>::infinity();require(!valid_appearance(bad));
    bad=a;bad.right_bone.clear();require(!valid_appearance(bad));
    require(!valid_contract({Message::ActorAppearance,Channel::Actor,Delivery::UnreliableSequenced,0,bytes}));
    HostPump host;ClientPump client;Identity token=100;host.start(500,1,{10,20},[&]{return ++token;});
    require(host.create_actor({90,1,1,1,3,a.body}));require(host.publish_appearance(a));
    auto link=MemoryTransport::pair();require(host.attach(1,std::move(link.second)));client.start(std::move(link.first),2,{10,20});
    for(unsigned n=0;n<6;++n){client.update(.1);host.update(.1);}client.update(.1);
    const auto player=client.session().welcome().player;require(player);
    require(host.set_interest_level(player,3));host.update(.1);client.update(.1);
    require(client.appearance(90)&&client.appearance(90)->weapon==a.weapon);
    ActorSnapshot snapshot;snapshot.entity=90;snapshot.generation=1;snapshot.level=3;snapshot.tick=1;snapshot.time_us=1000000;
    require(host.publish_snapshot(snapshot));client.update(.1);
    a.body="actors/exoskeleton.ogf";a.weapon.clear();a.hidden_weapon.clear();a.animation=0;
    require(host.publish_appearance(a));host.update(.1);client.update(.1);
    require(client.appearance(90)&&client.appearance(90)->body==a.body&&client.appearance(90)->weapon.empty());
    require(client.actors().find(90)->generation==1);
    ActorSnapshot sample;require(client.actors().sample(90,1100000,sample));
    require(host.set_interest_level(player,4));host.update(.1);client.update(.1);require(!client.appearance(90));
    require(host.set_interest_level(player,3));host.update(.1);client.update(.1);
    require(client.appearance(90)&&client.appearance(90)->body==a.body);
    require(host.remove_actor(90,1));host.update(.1);client.update(.1);require(!client.appearance(90));
    require(!host.publish_appearance(a));client.stop();host.stop();
    std::cout<<"Actor appearance validation, late join, gear changes, relevance and cleanup passed\n";
}
