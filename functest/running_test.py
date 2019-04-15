#!/usr/bin/env python

import sys
import os
import re
import time
from contextlib import contextmanager
from collections import defaultdict
import simplejson as json

from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.moduledeps import moduleDeps, pathCheck, TUN
from mininet.node import Controller
import mininet.topo

import tree_topo


from mininet.log import info, error, debug, output, warn
def timeout_iperf( self, hosts=None, l4Type='TCP', udpBw='10M', fmt=None,
           seconds=5, port=5001, timeout=5):
    """Run iperf between two hosts.
       hosts: list of hosts; if None, uses first and last hosts
       l4Type: string, one of [ TCP, UDP ]
       udpBw: bandwidth target for UDP test
       fmt: iperf format argument if any
       seconds: iperf time to transmit
       port: iperf port
       returns: two-element array of [ server, client ] speeds
       note: send() is buffered, so client rate can be much higher than
       the actual transmission rate; on an unloaded system, server
       rate should be much closer to the actual receive rate"""
    hosts = hosts or [ self.hosts[ 0 ], self.hosts[ -1 ] ]
    assert len( hosts ) == 2
    client, server = hosts
    output( '*** Iperf: testing', l4Type, 'bandwidth between',
            client, 'and', server, '\n' )
    server.cmd( 'timeout 10 killall -9 iperf' )
    output('kill all iperf')
    iperfArgs = 'timeout %d iperf -p %d ' % (timeout, port)
    bwArgs = ''
    if l4Type == 'UDP':
        iperfArgs += '-u '
        bwArgs = '-b ' + udpBw + ' '
    elif l4Type != 'TCP':
        raise Exception( 'Unexpected l4 type: %s' % l4Type )
    if fmt:
        iperfArgs += '-f %s ' % fmt
    start_time = time.time()
    server.sendCmd( iperfArgs + '-s' )
    output('iperf started')
    if l4Type == 'TCP':
        if not waitListening( client, server.IP(), port ):
            raise Exception( 'Could not connect to iperf on port %d'
                             % port )
    cliout = client.cmd( iperfArgs + '-t %d -c ' % seconds +
                         server.IP() + ' ' + bwArgs )
    output( 'Client output: %s\n' % cliout )
    servout = ''
    # We want the last *b/sec from the iperf server output
    # for TCP, there are two of them because of waitListening
    count = 2 if l4Type == 'TCP' else 1
    while len( re.findall( '/sec', servout ) ) < count:
        if time.time() - start_time > timeout:
            break
        output('waiting...')
        servout += server.monitor( timeoutms=500 )
    server.sendInt()
    servout += server.waitOutput()
    output( 'Server output: %s\n' % servout )
    result = [ self._parseIperf( servout ), self._parseIperf( cliout ) ]
    if l4Type == 'UDP':
        result.insert( 0, udpBw )
    output( '*** Results: %s\n' % result )
    return result

Mininet.timeout_iperf = timeout_iperf


class dRUNOS(Controller):
    def __init__(
            self,
            name,
            settings='network-settings.json',
            profile='default',
            port=6652,
            **kwargs
    ):
        Controller.__init__(
            self,
            name,
            port=port,
            command='runos',
            cargs='--config %s --profile %s --unusedport %s' % (settings, profile, "%s"),
            **kwargs
        )



class RUNOS(Controller):
    """ original runos """
    def __init__(
            self,
            name,
            settings='network-settings-maple.json',
            port=6652,
            **kwargs
    ):
        """
        WATNING: Be aware, that settings file must starts with number of port!
        default: 6652_network-settings.json
        Yeah this is hack, but the easiest way to connect mininet script and RUNOS
        """
        Controller.__init__(
            self,
            name,
            port=port,
            command='orig_runos',
            cargs=settings,
            **kwargs
        )

    def start( self ):
        """
        mininet forces to put port in cargs string. hate it
        """
        pathCheck( self.command )
        cout = '/tmp/' + self.name + '.log'
        if self.cdir is not None:
            self.cmd( 'cd ' + self.cdir )
        self.cmd( self.command + ' ' + self.cargs +
                  ' 1>' + cout + ' 2>' + cout + ' &' )
        self.execed = False


class FRENETIC(Controller):
    def __init__(
            self,
            name,
            port=6652,
            **kwargs
    ):
        Controller.__init__(
            self,
            name,
            port=port,
            command='frenetic',
            cargs='http-controller --openflow-port %s',
            **kwargs)


class FreneticApplication(Controller):
    """ Use frenetic python application as controller """
    def __init__(
        self,
        name,
        script,
        **kwargs
    ):
        Controller.__init__(
            self,
            name,
            command='python',
            port=9999,
            cargs='{}'.format(script),
            **kwargs
        )

    def start( self ):
        """
        mininet forces to put port in cargs string. hate it
        """
        pathCheck( self.command )
        cout = '/tmp/' + self.name + '.log'
        if self.cdir is not None:
            self.cmd( 'cd ' + self.cdir )
        self.cmd( self.command + ' ' + self.cargs +
                  ' 1>' + cout + ' 2>' + cout + ' &' )
        self.execed = False


def profiled_drunos(profile):
    return lambda name: dRUNOS(name, profile=profile)

def runos(name):
    return RUNOS(name)

def frenetic(script):
    return [lambda name: FRENETIC(name), lambda name: FreneticApplication(name, script=script)]


@contextmanager
def run_mininet(*args, **kwargs):
    try:
        net = Mininet(*args, **kwargs)
        net.start()
        net.waitConnected(delay=0.3)
        time.sleep(3) # give controller time to start
        yield net
    finally:
        try:
            net.stop()
        except Exception as e:
            print('Unexpecting error: {}'.format(e))
            print('Call the clear mininet')
            os.system('mn -c')
            raise


class SmartSum(object):
    def __init__(self, first, second):
        self.trigged = False
        self.first = first
        self.second = second

    def __call__(self, text):
        def helper(line):
            if self.trigged and line.count(self.second) > 0:
                self.trigged = False
                return 1
            if line.count(self.first) > 0 :
                self.trigged = True
            else :
                self.trigged = False
            return 0

        ret =  reduce(lambda x,y : x + helper(y), text.split('\n'), 0)
        return ret


def deadlined_iperf(net, port, timeout=5):
    server = net.hosts[0]
    client = net.hosts[-1]
    iperf = lambda: net.iperf(seconds=1, port=port)
    t = threading.Thread(target=iperf)
    t.start()
    t.join(3)
    server.cmd('killall -9 iperf')
    client.cmd('killall -9 iperf')
    t.join()


def run_snooping(net, messages):
    files = []
    for sw in net.switches:
        filename = "/tmp/snoop_{}.dmp".format(sw.dpid)

        # running snoop in daemon (&) output in filename
        out_cmd = '> {} &'.format(filename)

        # Since snoop print in stderr, mode stderr to stoud, and stdout to deb null
        # Grep by needed strings
        sw.dpctl('snoop', '2>&1 >/dev/null | grep -E "{}" {}'.format("|".join(messages), out_cmd))
        files.append(filename)
    return files

def count_flows(net):
    ret = 0
    for sw in net.switches:
        ftable = sw.dpctl('dump-flows')
        ret += len(ftable.split('\n'))
    return ret


def parse_snoop_files(files, messages):
    ret = defaultdict(lambda: 0)
    lldp_packet_ins = SmartSum("OFPT_PACKET_IN", "dl_type=0x88cc")
    lldp_packet_outs = SmartSum("OFPT_PACKET_OUT", "dl_type=0x88cc")
    for filename in files:
        with open(filename) as f:
            for line in f:
                for msg in messages:
                    ret[msg] += line.count(msg)
                ret['lldp_packet_ins'] += lldp_packet_ins(line)
                ret['lldp_packet_outs'] += lldp_packet_outs(line)
    return ret



def run_example(topo, prefix, controller):
    try:
        topo.dsh_name
    except AttributeError:
        print("Topo has no dsh_name")
        raise

    print ("Run example: {}, nodes: {}", topo.dsh_name, len(topo.nodes()))
    messages = [
        'OFPT_FLOW_MOD',
        'OFPT_PACKET_IN',
        'dl_type=0x88cc',
        'OFPT_PACKET_OUT',
        'OFPT_FLOW_REMOVED',
        'OFPT_GROUP_MOD',
    ]
    with run_mininet(topo, autoStaticArp=True, controller=controller) as net:
        print("start: {}: nodes {}, hosts {}".format(topo.dsh_name, len(topo.nodes()), len(topo.hosts())))
        files = run_snooping(net, messages)
        print("Snooping started")
        proactive_count = count_flows(net)
        print("Proactive flows: {}".format(proactive_count))
        start_time = time.time()
        loss = net.pingAll(timeout=1)
        net.iperf(seconds=1, port=3000) # pass
        for port in range(2220, 2225):
            try:
                net.timeout_iperf(l4Type='UDP', port=port, seconds=1, timeout=2)
            except Exception as e:
                print(e)
                raise
        result = parse_snoop_files(files, messages)
        result['loss'] = loss
        result['hosts'] = len(topo.hosts())
        result['nodes'] = len(topo.nodes())
        result['topo'] = topo.dsh_name
        result['proactive_count'] = proactive_count
        print(result)
        result_fule = '{}.json'.format(prefix)
        data = []
        try:
            with open(result_fule, 'r') as f:
                data = json.load(f)
        except:
            # if file is not exist
            pass

        data.append(result)
        with open(result_fule, 'w') as f:
            json.dump(data, f, sort_keys=True, indent=4*' ')

def dispatch_test_type(test_type, sopts, min_k, max_k):
    assert max_k >= 4
    max_k += 1
    if test_type == 'linear':
        gen_topo = (mininet.topo.LinearTopo(k=k, n=1,sopts=sopts) for k in range(min_k, max_k))
    elif test_type == 'single':
        gen_topo = (mininet.topo.SingleSwitchTopo(k=k,sopts=sopts) for k in range(min_k, max_k))
    elif test_type == 'tree':
        gen_topo = (tree_topo.BinTreeTopo(n=k,sopts=sopts) for k in range(min_k, max_k))

    for t in gen_topo:
        t.dsh_name = test_type
        yield t


if __name__ == '__main__':
    try:
        sys.argv[1]
    except IndexError:
        print("Specify the test: frenetic, drunos or maple")
        sys.exit(1)

    result_file = 'results/result_' + sys.argv[1]
    if sys.argv[1] == 'frenetic':
        sopts = {'protocols': 'OpenFlow10'}
        controller = frenetic('firwall_learning.py')
    else:
        sopts = {'protocols': 'OpenFlow13'}
        if sys.argv[1] == 'drunos':
            controller = profiled_drunos('running_example')
        elif sys.argv[1] == 'maple':
            controller = runos

    try:
        test_type = sys.argv[2]
    except IndexError:
        print ("Not specified test type. Used default: Linear")
        test_type = 'Linear'

    try:
        max_k = int(sys.argv[3])
    except IndexError:
        print ("Max k not specified. Use 4")
        max_k = 4

    try:
        min_k = int(sys.argv[4])
    except IndexError:
        min_k = 4

    for topo in dispatch_test_type(test_type, sopts, min_k, max_k):
        try:
            run_example(topo, prefix=result_file, controller=controller)
        except KeyboardInterrupt:
            if 'y' not in input("Continue?").lower():
                raise
