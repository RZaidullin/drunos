#pragma once

#include <functional>
#include <list>
#include <set>
#include <tuple>
#include <chrono>
#include <boost/variant.hpp>
#include <boost/variant/recursive_wrapper_fwd.hpp>
#include <boost/variant/static_visitor.hpp>
#include "api/Packet.hh"
#include "oxm/openflow_basic.hh"

namespace runos {
namespace retic {

typedef std::chrono::duration<uint32_t> duration;

class Filter {
public:
    oxm::field<> field;
};

class Stop { };

class Id { };

struct Modify {
    oxm::field<> field;
};

struct FlowSettings {
    duration idle_timeout = duration::max();
    duration hard_timeout = duration::max();
    inline friend FlowSettings operator&(const FlowSettings& lhs, const FlowSettings& rhs) {
        FlowSettings ret;
        ret.idle_timeout = std::min(lhs.idle_timeout, rhs.idle_timeout);
        ret.hard_timeout = std::min(lhs.hard_timeout, rhs.hard_timeout);

        // hard timeout cann't be less than idle timeout
        ret.idle_timeout = std::min(ret.idle_timeout, ret.hard_timeout);
        return ret;
    }
};

struct Sequential;
struct Parallel;
struct PacketFunction;

using policy =
    boost::variant<
        Stop,
        Id,
        Filter,
        Modify,
        FlowSettings,
        boost::recursive_wrapper<PacketFunction>,
        boost::recursive_wrapper<Sequential>,
        boost::recursive_wrapper<Parallel>
    >;

struct PacketFunction {
    uint64_t id;
    std::function<policy(Packet& pkt)> function;
};

struct Sequential {
    policy one;
    policy two;
};

struct Parallel {
    policy one;
    policy two;
};

inline
policy modify(oxm::field<> field) {
    return Modify{field};
}

inline
policy fwd(uint32_t port) {
    static constexpr auto out_port = oxm::out_port();
    return modify(out_port << port);
};

inline
policy stop() {
    return Stop();
}

inline
policy id() {
    return Id();
}

inline
policy filter(oxm::field<> f)
{
    return Filter{f};
}

inline
policy handler(std::function<policy(Packet&)> function)
{
    static uint64_t id_gen = 0;
    return PacketFunction{id_gen++, function};
}

inline
policy idle_timeout(duration time) {
    return FlowSettings{.idle_timeout = time};
}

inline
policy hard_timeout(duration time) {
    return FlowSettings{time, time};
}

inline
policy operator>>(policy lhs, policy rhs)
{
    return Sequential{lhs, rhs};
}

inline
policy operator+(policy lhs, policy rhs)
{
    return Parallel{lhs, rhs};
}

// This is only for purpose that seq operator must have higher priorty that parallel operator
inline
policy operator|(policy lhs, policy rhs)
{
    return Parallel{lhs, rhs};
}

// Operators
inline bool operator==(const Stop&, const Stop&) {
    return true;
}

inline bool operator==(const Id&, const Id&) {
    return true;
}

inline bool operator==(const Filter& lhs, const Filter& rhs) {
    return lhs.field == rhs.field;
}

inline bool operator==(const Modify& lhs, const Modify& rhs) {
    return lhs.field == rhs.field;
}

inline bool operator==(const PacketFunction& lhs, const PacketFunction& rhs) {
    return lhs.id == rhs.id;
}

inline bool operator==(const Sequential& lhs, const Sequential& rhs) {
    return lhs.one == rhs.one && lhs.two == rhs.two;
}

inline bool operator==(const Parallel& lhs, const Parallel& rhs) {
    // TODO: this function needs test. A fully test for such cases (a + b + c) == (b + c + a) == (c + b + a) ... etc
    return (lhs.one == rhs.one && lhs.two == rhs.two) ||
           (lhs.one == rhs.two && lhs.two == rhs.one);
}

inline bool operator==(const FlowSettings& lhs, const FlowSettings& rhs) {
    return lhs.idle_timeout == rhs.idle_timeout && lhs.hard_timeout == rhs.hard_timeout;
}

inline std::ostream& operator<<(std::ostream& out, const Filter& fil) {
    return out << "filter( " << fil.field << " )";
}

inline std::ostream& operator<<(std::ostream& out, const Stop& stop) {
    return out << "stop";
}

inline std::ostream& operator<<(std::ostream& out, const Id&) {
    return out << "id";
}

inline std::ostream& operator<<(std::ostream& out, const Modify& mod) {
    return out << "modify( " << mod.field << " )";
}

inline std::ostream& operator<<(std::ostream& out, const Sequential& seq) {
    return out << seq.one << " >> " << seq.two;
}

inline std::ostream& operator<<(std::ostream& out, const Parallel& par) {
    return out << par.one << " + " << par.two;
}

inline std::ostream& operator<<(std::ostream& out, const PacketFunction& func) {
    return out << " function ";
}

inline std::ostream& operator<<(std::ostream& out, const FlowSettings& flow) {
    return out 
        << "{ idle_timeout = " << std::chrono::seconds(flow.idle_timeout).count() << " sec. "
        << "hard_timeout = " << std::chrono::seconds(flow.hard_timeout).count() << " sec. }";
}

} // namespace retic
} // namespace runos
