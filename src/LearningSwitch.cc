/*
 * Copyright 2015 Applied Research Center for Computer Networks
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "LearningSwitch.hh"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <boost/optional.hpp>
#include <boost/thread.hpp>

#include "api/Packet.hh"
#include "api/PacketMissHandler.hh"
#include "api/TraceablePacket.hh"
#include "types/ethaddr.hh"
#include "oxm/openflow_basic.hh"

#include "retic/policies.hh"

#include "Topology.hh"
#include "SwitchConnection.hh"
#include "Flow.hh"
#include "STP.hh"
#include "Maple.hh"
#include "Decision.hh"
#include "Common.hh"
#include "Retic.hh"


// REGISTER_APPLICATION(LearningSwitch, {"maple", "topology", "stp", ""})
REGISTER_APPLICATION(LearningSwitch, {"retic", "topology", "stp", ""})

using namespace runos;

// pure maple route
class Route : public Decision::CustomDecision {
    struct Ports {
        uint32_t inport;
        uint32_t outport;
    };

    std::unordered_map<uint64_t, Ports> ports;
public:

    /* route must contains only outports */
    Route(data_link_route route)
    {
        if (route.size() % 2 != 0){
            RUNOS_THROW( invalid_argument() );
        }

        for (auto it = route.begin(); it != route.end(); it += 2){
            if (it->dpid != (it+1)->dpid){
                RUNOS_THROW( invalid_argument() );
            }
            ports[it->dpid].inport = it->port;
            ports[it->dpid].outport = (it+1)->port;
        }
    }

    std::vector<uint64_t> switches() const override
    {
        std::vector<uint64_t> ret;
        for (auto i : ports){
            ret.push_back(i.first);
        }
        return ret;
    }

    void apply(ActionList& ret, uint64_t dpid) override
    {
        ret.add_action(new of13::OutputAction(ports[dpid].outport, 0));
    }

    std::vector<std::pair<uint64_t,
                          uint32_t>> const
    in_ports() override
    {
        std::vector<std::pair<uint64_t, uint32_t>> ret;
        for (auto i : ports) {
            ret.push_back({i.first, i.second.inport});
        }
        return ret;
    }

};

using link_pair = std::pair<std::pair<uint64_t,uint64_t>,std::pair<uint64_t,uint64_t>>;
std::map<link_pair, std::vector<uint64_t>> link_tag;

uint16_t getTag(oxm::switch_id id1, oxm::out_port outport, oxm::switch_id id2, oxm::in_port inport){
    oxm::field_set tmp1;
    oxm::field_set tmp2;
    tmp1.modify(oxm::field<>(id1));
    tmp1.modify(oxm::field<>(outport));
    tmp2.modify(oxm::field<>(id2));
    tmp2.modify(oxm::field<>(inport));
    uint64_t sw1,sw2,iport,oport;
    Packet& pkt1(tmp1);
    Packet& pkt2(tmp2);
    sw1 = pkt1.load(oxm::switch_id());
    oport = pkt1.load(oxm::out_port());
    sw2 = pkt2.load(oxm::switch_id());
    iport = pkt2.load(oxm::in_port());
    link_pair lnk({sw1,oport},{sw2,iport});
    auto m_tags = link_tag[lnk];
    std::vector<uint64_t> used_tags;
    if(!m_tags.empty())
        for (auto it : m_tags){
            used_tags.push_back(it);
        }
    std::sort(used_tags.begin(),used_tags.end());
    uint64_t prev = 1;
    for (auto it : used_tags){
        if (it == prev){prev++;};
        if (it > prev){break;}
    }
    link_tag[lnk].push_back(prev);
    return prev;
}

retic::policy route_policy(data_link_route route) {
    using namespace retic;

    const auto in_port = oxm::in_port();
    const auto switch_id = oxm::switch_id();
    //static constexpr auto vlan_vid = oxm::vlan_vid();
    //oxm::field<oxm::vlan_vid> vl;

    if (route.size() % 2 != 0){
        RUNOS_THROW( invalid_argument() );
    }

    policy p;
    uint16_t prev = 1, vlan;
    int beg = 1, good = 1;
    for (auto it = route.begin(); it != route.end(); it += 2){
        if (it->dpid != (it+1)->dpid){
            RUNOS_THROW( invalid_argument() );
        }
        if(beg and good){
        //vlan_vid << vlan;
            uint32_t inport = it->port;
            uint32_t outport = (it+1)->port;
            p = p + (filter(switch_id == it->dpid) >>
                     filter(in_port == inport) >>
                     >> fwd(outport));
            beg = 0;
        }
        else{
            uint32_t inport = it->port;
            uint32_t outport = (it+1)->port;
            vlan = getTag(it->dpid, it->port, (it+1)->port);
            if(!good){
                p = p + (filter(switch_id == it->dpid) >>
                         filter(in_port == inport) >> filter(oxm::vlan_vid() == prev) >>
                         modify(oxm::vlan_vid() << vlan) >> fwd(outport));
                good = 1;
            }
            else{
                p = p + (filter(switch_id == it->dpid) >>
                         filter(in_port == inport) >>
                         modify(oxm::vlan_vid() << vlan) >> fwd(outport));
            }
            prev = vlan;
        }
    }
    return p;
}

class HostsDatabase {
    boost::shared_mutex mutex;
    std::unordered_map<ethaddr, switch_and_port> db;

public:
    void learn(uint64_t dpid, uint32_t in_port, ethaddr mac)
    {
        if (is_broadcast(mac)) { // should we test here??
            DLOG(WARNING) << "Broadcast source address detected";
            return;
        }

        VLOG(5) << mac << " seen at " << dpid << ':' << in_port;
        {
            boost::unique_lock< boost::shared_mutex > lock(mutex);
            db.emplace(mac, switch_and_port{dpid, in_port});
        }
    }

    boost::optional<switch_and_port> query(ethaddr mac)
    {
        boost::shared_lock< boost::shared_mutex > lock(mutex);

        auto it = db.find(mac);
        if (it != db.end())
            return it->second;
        else
            return boost::none;
    }
};

std::ostream& operator << (std::ostream &out,const data_link_route &route){
    out << " [ ";
    for (auto p : route) {
        out << "(dpid : " << p.dpid << ", port : " << p.port << ")";
    }
    out << " ] ";
    return out;
}

void LearningSwitch::init(Loader *loader, const Config &)
{
    auto topology = Topology::get(loader);
    auto db = std::make_shared<HostsDatabase>();
    LOG(INFO) << "db initialized";
    m_stp = STP::get(loader);

    const auto ofb_in_port = oxm::in_port();
    const auto ofb_eth_src = oxm::eth_src();
    const auto ofb_eth_dst = oxm::eth_dst();
    const auto switch_id = oxm::switch_id();

        // TODO: enable maple driven
//    auto maple = Maple::get(loader);
//
//    maple->registerHandler("forwarding",
//        [=](Packet& pkt, FlowPtr, Decision decision) {
//            // Get required fields
//            auto tpkt = packet_cast<TraceablePacket>(pkt);
//            ethaddr dst_mac = pkt.load(ofb_eth_dst);
//            ethaddr src_mac = tpkt.watch(ofb_eth_src);
//            uint64_t dpid;
//            uint32_t inport;
//            std::tie(dpid, inport) = tpkt.vload(switch_id, ofb_in_port);
//
//            db->learn(dpid,
//                      tpkt.watch(ofb_in_port),
//                      src_mac);
//
//            auto target = db->query(dst_mac);
//            auto source = db->query(src_mac);
//
//            // Forward
//            if (target) {
//                auto route = topology
//                             ->computeRoute(source->dpid, target->dpid);
//                if (not route.empty() or target->dpid == source->dpid){
//                    route.insert(route.begin(), *source);
//                    route.push_back(*target);
//                    DVLOG(10) << "Forwarding packet from " << source->dpid
//                              << "to " << target->dpid << " through route : "
//                              << route;
//                    return decision.custom(
//                            Decision::CustomDecisionPtr(new Route(route)))
//                            .idle_timeout(std::chrono::seconds(20*60))
//                            .hard_timeout(std::chrono::minutes(30));
//                } else {
//                    LOG(WARNING)
//                        << "Path from " << source->dpid
//                        << "to " << target->dpid << "not found";
//                    return decision.drop();
//                }
//            } else {
//                if (not is_broadcast(dst_mac)) {
//                    VLOG(5) << "Flooding for unknown address " << dst_mac;
//                    return decision.custom(std::make_shared<STP::Decision>())
//                            .idle_timeout(std::chrono::seconds::zero());
//                }
//                return decision.custom(std::make_shared<STP::Decision>());
//            }
//    });
    auto retic = Retic::get(loader);
    using namespace retic;
    m_policy = handler([=](Packet& pkt) {
            // Get required fields
            auto tpkt = packet_cast<TraceablePacket>(pkt);
            ethaddr dst_mac = pkt.load(ofb_eth_dst);

            ethaddr src_mac = pkt.load(ofb_eth_src);
            uint64_t dpid = tpkt.watch(switch_id);
            uint32_t inport = tpkt.watch(ofb_in_port);

            db->learn(dpid,
                      inport,
                      src_mac);

            auto target = db->query(dst_mac);
            auto source = db->query(src_mac);
            //LOG(INFO) << "Routing from: " << source->dpid << " to: " << target->dpid;

            // Forward
            if (target) {
                LOG(INFO) << "Routing from: " << source->dpid << " to: " << target->dpid;
                auto route = topology
                             ->computeRoute(source->dpid, target->dpid);
                if (not route.empty() or target->dpid == source->dpid){
                    route.insert(route.begin(), *source);
                    route.push_back(*target);
                    DVLOG(10) << "Forwarding packet from " << source->dpid
                              << "to " << target->dpid << " through route : "
                              << route;
                    LOG(INFO) << "Forwarding packet from " << source->dpid
                              << "to " << target->dpid << " through route : "
                              << route;
                    return route_policy(route);
                } else {
                    LOG(WARNING)
                        << "Path from " << source->dpid
                        << "to " << target->dpid << "not found";
                    return stop();
                }
            } else {
                if (not is_broadcast(dst_mac)) {
                    // TODO: unhardcode
                    return idle_timeout(std::chrono::seconds::zero()) >> (
                        m_stp->broadcastPolicy()
                    );
                }
                return m_stp->broadcastPolicy() ;
            }
    });

    retic->registerPolicy("forwarding", m_policy);
}
