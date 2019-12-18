/* -------------------------------------------------------------------------- */
/* Copyright 2002-2019, OpenNebula Project, OpenNebula Systems                */
/*                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may    */
/* not use this file except in compliance with the License. You may obtain    */
/* a copy of the License at                                                   */
/*                                                                            */
/* http://www.apache.org/licenses/LICENSE-2.0                                 */
/*                                                                            */
/* Unless required by applicable law or agreed to in writing, software        */
/* distributed under the License is distributed on an "AS IS" BASIS,          */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
/* See the License for the specific language governing permissions and        */
/* limitations under the License.                                             */
/* -------------------------------------------------------------------------- */

#include "Monitor.h"
#include "MonitorDriver.h"
#include "MonitorConfigTemplate.h"
#include "NebulaLog.h"
#include "Client.h"
#include "StreamManager.h"
#include "SqliteDB.h"
#include "MySqlDB.h"

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

using namespace std;

void Monitor::start()
{
    // -------------------------------------------------------------------------
    // Configuration File
    // -------------------------------------------------------------------------
    config.reset(new MonitorConfigTemplate(get_defaults_location(),
                conf_filename));

    if (config->load_configuration() != 0)
    {
        throw runtime_error("Error reading configuration file.");
    }

    // Log system
    NebulaLog::LogType log_system = get_log_system(NebulaLog::STD);
    Log::MessageType clevel       = get_debug_level(Log::WARNING);

    if (log_system != NebulaLog::UNDEFINED)
    {
        string log_file = get_log_location() + "monitor.log";
        NebulaLog::init_log_system(log_system,
                                   clevel,
                                   log_file.c_str(),
                                   ios_base::trunc,
                                   "one_monitor");
    }
    else
    {
        throw runtime_error("Unknown LOG_SYSTEM.");
    }

    NebulaLog::info("MON", "Init Monitor Log system");

    ostringstream oss;
    oss << "Starting Monitor Daemon" << endl;
    oss << "----------------------------------------\n";
    oss << "       Monitor Configuration File       \n";
    oss << "----------------------------------------\n";
    oss << *config;
    oss << "----------------------------------------";

    NebulaLog::info("MON", oss.str());

    // -------------------------------------------------------------------------
    // XML-RPC Client
    // -------------------------------------------------------------------------
    string       one_xmlrpc;
    long long    message_size;
    unsigned int timeout;

    config->get("ONE_XMLRPC", one_xmlrpc);
    config->get("MESSAGE_SIZE", message_size);
    config->get("TIMEOUT", timeout);

    Client::initialize("", one_xmlrpc, message_size, timeout);

    oss.str("");

    oss << "XML-RPC client using " << (Client::client())->get_message_size()
        << " bytes for response buffer.\n";

    NebulaLog::log("MON", Log::INFO, oss);

    xmlInitParser();

    // -------------------------------------------------------------------------
    // Database
    // -------------------------------------------------------------------------
    const VectorAttribute * _db = config->get("DB");

    std::string db_backend = _db->vector_value("BACKEND");

    if (db_backend == "sqlite")
    {
        sqlDB.reset(new SqliteDB(get_var_location() + "one.db"));
    }
    else
    {
        string server;
        int    port;
        string user;
        string passwd;
        string db_name;
        string encoding;
        int    connections;

        _db->vector_value<string>("SERVER", server, "localhost");
        _db->vector_value("PORT", port, 0);
        _db->vector_value<string>("USER", user, "oneadmin");
        _db->vector_value<string>("PASSWD", passwd, "oneadmin");
        _db->vector_value<string>("DB_NAME", db_name, "opennebula");
        _db->vector_value<string>("ENCODING", encoding, "");
        _db->vector_value("CONNECTIONS", connections, 50);

        sqlDB.reset(new MySqlDB(server, port, user, passwd, db_name,
                    encoding, connections));
    }

    // -------------------------------------------------------------------------
    // Pools
    // -------------------------------------------------------------------------
    time_t host_exp;

    config->get("HOST_MONITORING_EXPIRATION_TIME", host_exp);

    hpool.reset(new HostRPCPool(sqlDB.get(), host_exp));
    vmpool.reset(new VMRPCPool(sqlDB.get()));

    // -------------------------------------------------------------------------
    // Close stds in drivers
    // -------------------------------------------------------------------------
    fcntl(0, F_SETFD, FD_CLOEXEC);
    fcntl(1, F_SETFD, FD_CLOEXEC);
    fcntl(2, F_SETFD, FD_CLOEXEC);

    one_util::SSLMutex::initialize();

    // -------------------------------------------------------------------------
    // Drivers
    // -------------------------------------------------------------------------
    std::string addr;
    unsigned int port;
    unsigned int threads;
    std::string pub_key;
    std::string pri_key;

    auto udp_conf = config->get("UDP_LISTENER");

    udp_conf->vector_value("ADDRESS", addr);
    udp_conf->vector_value("PORT", port);
    udp_conf->vector_value("THREADS", threads);
    udp_conf->vector_value("PUBKEY", pub_key);
    udp_conf->vector_value("PRIKEY", pri_key);

    vector<const VectorAttribute *> drivers_conf;

    config->get("IM_MAD", drivers_conf);

    int timer_period;
    int monitor_interval_host;
    config->get("MANAGER_TIMER", timer_period);
    config->get("MONITORING_INTERVAL_HOST", monitor_interval_host);

    init_rsa_keys(pub_key, pri_key);

    // Replace the PUBKEY with the content of the PUBKEY file
    ifstream f(pub_key);
    if (f.good())
    {
        stringstream buffer;
        buffer << f.rdbuf();
        udp_conf->replace("PUBKEY", buffer.str());
    }

    hm.reset(new HostMonitorManager(hpool.get(),
                addr, port, threads,
                get_mad_location(),
                timer_period,
                monitor_interval_host));

    if (hm->load_monitor_drivers(drivers_conf) != 0)
    {
        NebulaLog::error("MON", "Unable to load monitor drivers");
        return;
    }

    // -------------------------------------------------------------------------
    // Start Drivers
    // -------------------------------------------------------------------------
    std::string error;

    MonitorDriverProtocol::hm = hm.get();

    if (hm->start(error) == -1)
    {
        NebulaLog::error("MON", "Error starting monitor drivers: " + error);
        return;
    }

    xmlCleanupParser();

    one_util::SSLMutex::finalize();

    NebulaLog::finalize_log_system();
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
