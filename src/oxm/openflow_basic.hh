#pragma once

#include <cstdint>
#include <type_traits>

#include "openflow/common.hh"
#include "types/ethaddr.hh"
#include "types/ipv4addr.hh"
#include "types/IPv6Addr.hh"
#include "types/printers.hh"
#include "type.hh"

namespace runos {
namespace oxm {

// TODO: unmaskable<type>

namespace detail {

template<class T,
      typename std::enable_if<std::is_integral<T>::value>::type* = nullptr
>
std::ostream& print(std::ostream& out, const bits<>& value) {
    return out << bit_cast<T>(value);
}

template<class T,
      typename std::enable_if<!std::is_integral<T>::value>::type* = nullptr
>
std::ostream& print(std::ostream& out, const bits<>& value) {
    return out << T((typename T::bits_type)(value));
}

}

struct switch_id : define_type  < switch_id
                                , uint16_t(of::oxm::ns::NON_OPENFLOW)
                                , uint8_t(of::oxm::non_openflow_fields::SWITCH_ID)
                                , 64, uint64_t, uint64_t, false, &detail::print<uint64_t> >
{ };

struct out_port : define_type < out_port
                              , uint16_t(of::oxm::ns::NON_OPENFLOW)
                              , uint8_t(of::oxm::non_openflow_fields::OUT_PORT)
                              , 32, uint32_t, uint32_t, false, &detail::print<uint32_t>>
{ };

template< class Final,
          of::oxm::basic_match_fields ID,
          size_t NBITS,
          class ValueType,
          class MaskType = ValueType,
          bool HASMASK = false >
using define_ofb_type =
    define_type< Final
               , uint16_t(of::oxm::ns::OPENFLOW_BASIC)
               , uint8_t(ID)
               , NBITS, ValueType, MaskType, HASMASK, &detail::print<ValueType> >;

template< class Final,
          of::oxm::basic_match_fields ID,
          size_t NBITS,
          type::print_f PRINT_F,
          class ValueType,
          class MaskType = ValueType,
          bool HASMASK = false >
using define_printable_ofb_type =
    define_type< Final
               , uint16_t(of::oxm::ns::OPENFLOW_BASIC)
               , uint8_t(ID)
               , NBITS, ValueType, MaskType, HASMASK, PRINT_F >;

struct in_port : define_ofb_type
     < in_port, of::oxm::basic_match_fields::IN_PORT, 32, uint32_t >
{ };

struct eth_type : define_printable_ofb_type
    < eth_type, of::oxm::basic_match_fields::ETH_TYPE, 16, &types::print_eth_type, uint16_t >
{ };
struct eth_src : define_ofb_type
    < eth_src, of::oxm::basic_match_fields::ETH_SRC, 48, ethaddr, ethaddr, true >
{ };
struct eth_dst : define_ofb_type
    < eth_dst, of::oxm::basic_match_fields::ETH_DST, 48, ethaddr, ethaddr, true >
{ };

struct ip_proto : define_printable_ofb_type
    < ip_proto, of::oxm::basic_match_fields::IP_PROTO, 8, &types::print_ip_proto, uint8_t >
{ };
// TODO: replace with ipaddr type
struct ipv4_src : define_ofb_type
    < ipv4_src, of::oxm::basic_match_fields::IPV4_SRC, 32, ipv4addr, ipv4addr, true >
{ };
struct ipv4_dst : define_ofb_type
    < ipv4_dst, of::oxm::basic_match_fields::IPV4_DST, 32, ipv4addr, ipv4addr, true >
{ };

struct tcp_src : define_ofb_type
    < tcp_src, of::oxm::basic_match_fields::TCP_SRC, 16, uint16_t >
{ };
struct tcp_dst : define_ofb_type
    < tcp_dst, of::oxm::basic_match_fields::TCP_DST, 16, uint16_t >
{ };

struct udp_src : define_ofb_type
    < udp_src, of::oxm::basic_match_fields::UDP_SRC, 16, uint16_t >
{ };
struct udp_dst : define_ofb_type
    < udp_dst, of::oxm::basic_match_fields::UDP_DST, 16, uint16_t >
{ };

// TODO: replace with ipaddr type
struct arp_spa : define_ofb_type
    < arp_spa, of::oxm::basic_match_fields::ARP_SPA, 32, ipv4addr, ipv4addr, true >
{ };
struct arp_tpa : define_ofb_type
    < arp_tpa, of::oxm::basic_match_fields::ARP_TPA, 32, ipv4addr, ipv4addr, true >
{ };
struct arp_sha : define_ofb_type
    < arp_sha, of::oxm::basic_match_fields::ARP_SHA, 48, ethaddr, ethaddr, true >
{ };
struct arp_tha : define_ofb_type
    < arp_tha, of::oxm::basic_match_fields::ARP_THA, 48, ethaddr, ethaddr, true >
{ };
struct arp_op : define_printable_ofb_type
    < arp_op, of::oxm::basic_match_fields::ARP_OP, 16, &types::print_arp_op, uint16_t >
{ };
struct vlan_vid : define_ofb_type
    < vlan_vid, of::oxm::basic_match_fields::VLAN_VID, 16, uint16_t >
{ };
struct ipv6_src : define_ofb_type
    < ipv6_src, of::oxm::basic_match_fields::IPV6_SRC, 128, IPv6Addr, IPv6Addr, true >
{ };
struct ipv6_dst : define_ofb_type
    < ipv6_dst, of::oxm::basic_match_fields::IPV6_DST, 128, IPv6Addr, IPv6Addr, true >
{ };
struct icmp_type : define_ofb_type
    < icmp_type, of::oxm::basic_match_fields::ICMPV4_TYPE, 8, uint8_t >
{ };
struct icmp_code : define_ofb_type
    < icmp_code, of::oxm::basic_match_fields::ICMPV4_CODE, 8, uint8_t >
{ };
}
}
