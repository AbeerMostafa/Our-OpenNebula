#!/usr/bin/env ruby

# -------------------------------------------------------------------------- #
# Copyright 2002-2019, OpenNebula Project, OpenNebula Systems                #
#                                                                            #
# Licensed under the Apache License, Version 2.0 (the "License"); you may    #
# not use this file except in compliance with the License. You may obtain    #
# a copy of the License at                                                   #
#                                                                            #
# http://www.apache.org/licenses/LICENSE-2.0                                 #
#                                                                            #
# Unless required by applicable law or agreed to in writing, software        #
# distributed under the License is distributed on an "AS IS" BASIS,          #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   #
# See the License for the specific language governing permissions and        #
# limitations under the License.                                             #
#--------------------------------------------------------------------------- #

require 'socket'
require 'base64'
require 'resolv'
require 'ipaddr'
require 'zlib'
require 'yaml'
require 'open3'
require 'openssl'

require 'rexml/document'


#  This class represents a monitord client. It handles udp and tcp connections
#  and send update messages to monitord
#
class MonitorClient

    # Defined in src/monitor/include/MonitorDriverMessages.h
    MESSAGE_TYPES = %w[MONITOR_VM MONITOR_HOST SYSTEM_HOST STATE_VM
                       START_MONITOR STOP_MONITOR].freeze

    MESSAGE_STATUS = { true =>'SUCCESS', false => 'FAILURE' }.freeze

    MESSAGE_TYPES.each do |mt|
        define_method(mt.downcase.to_sym) do |rc, payload|
            msg = "#{mt} #{MESSAGE_STATUS[rc]} #{@hostid} #{pack(payload)}"
            @socket_udp.send(msg, 0)
        rescue StandardError
        end
    end

    # Options to create a monitord client
    # :host [:String] to send the messages to
    # :port [:String] of monitord server
    # :hostid [:String] OpenNebula ID of this host
    # :pubkey [:String] public key to encrypt messages
    def initialize(server, port, id, opt = {})
        @opts = {
            :pubkey => ''
        }.merge opt

        addr = Socket.getaddrinfo(server, port)[0]

        @socket_udp = UDPSocket.new(addr[0])
        @socket_udp.connect(addr[3], addr[1])

        if @opts[:pubkey].empty?
            @pubkey = nil
        else
            @pubkey = OpenSSL::PKey::RSA.new @opts[:pubkey]
        end

        @hostid = id
    end

    private

    # Formats message payload to send over the wire
    def pack(data)
        zdata  = Zlib::Deflate.deflate(data, Zlib::BEST_COMPRESSION)
        data64 = Base64.strict_encode64(zdata)

        if @pubkey
            @key_pub.public_encrypt(data64)
        else
            data64
        end
    end

end

#  This class wraps the execution of a probe directory and sends data to
#  monitord (optionally)
#
class ProbeRunner

    def initialize(hyperv, path, stdin)
        @path  = File.join(File.dirname(__FILE__), '..', "#{hyperv}-probes.d",
                          path)
        @stdin = stdin
    end

    def run_probes
        data = ''

        Dir.each_child(@path) do |probe|
            probe_path = File.join(@path, probe)

            next unless File.executable?(probe_path)

            o, e, s = Open3.capture3(probe_path, :stdin_data => @stdin)

            data += o

            return [-1, "Error executing #{probe}: #{e}"] if s.exitstatus != 0
        end

        [0, data]
    end

    def self.run_once(hyperv, path, stdin)
        runner = ProbeRunner.new(hyperv, path, stdin)
        runner.run_probes
    end

    def self.monitor_loop(hyperv, path, period, stdin, &block)
        runner = ProbeRunner.new(hyperv, path, stdin)

        loop do
            ts = Time.now

            rc, data = runner.run_probes

            block.call(rc, data)

            run_time = (Time.now - ts).to_i

            sleep(period.to_i - run_time) if period.to_i > run_time
        end
    end

end

#-------------------------------------------------------------------------------
# Configuration (from monitord)
#-------------------------------------------------------------------------------
xml_txt = STDIN.read

begin
    config = REXML::Document.new(xml_txt).root

    host   = config.elements['UDP_LISTENER/MONITOR_ADDRESS'].text.to_s
    port   = config.elements['UDP_LISTENER/PORT'].text.to_s
    pubkey = config.elements['UDP_LISTENER/PUBKEY'].text.to_s
    hostid = config.elements['HOST_ID'].text.to_s
    hyperv = ARGV[0].split(' ')[0]


    probes = {
        :system_host => {
            :period => config.elements['PROBES_PERIOD/SYSTEM_HOST'].text.to_s,
            :path => 'host/system'
        },

        :monitor_host => {
            :period => config.elements['PROBES_PERIOD/MONITOR_HOST'].text.to_s,
            :path => 'host/monitor'
        },

        :state_vm => {
            :period => config.elements['PROBES_PERIOD/STATUS_VM'].text.to_s,
            :path => 'vm/status'
        },

        :monitor_vm => {
            :period => config.elements['PROBES_PERIOD/MONITOR_VM'].text.to_s,
            :path => 'vm/monitor'
        },
    }
rescue StandardError => e
    puts e.inspect
    exit(-1)
end

#-------------------------------------------------------------------------------
# Run configuration probes and send information to monitord
#-------------------------------------------------------------------------------
client = MonitorClient.new(host, port, hostid, :pubkey => pubkey)

rc, data = ProbeRunner.run_once(hyperv, probes[:system_host][:path], xml_txt)

puts data

exit(-1) if rc == -1

#-------------------------------------------------------------------------------
# Start monitor threads and shepherd
#-------------------------------------------------------------------------------
Process.setsid

STDIN.close

_rd, wr = IO.pipe

STDOUT.reopen(wr)
STDERR.reopen(wr)

threads = []

probes.each do |msg_type, conf|
    threads << Thread.new {
        ProbeRunner.monitor_loop(hyperv, conf[:path], conf[:period], xml_txt) do |rc, da|
            client.send(msg_type, rc == 0, da)
        end
    }
end

threads.each {|thr| thr.join }
