#pragma once

#include <string>
#include <map>
#include <atomic>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include "../node.hpp"

using json = nlohmann::json;
using namespace std;

struct NodeInfo {
    vector<string> topics;
    int secondsSinceLastHeartbeat;
};

class CentralNameServer : public GenericNode {
    private:
        int m_port;
        zmq::socket_t m_socket;
        string m_log_name;
        vector<NodeInfo> m_registered_nodes;
        map<string, string> m_registered_topics;
        map<string, string> m_data_storage;

    public:
        CentralNameServer(string ip_address, int port, string master_ip_address);
        ~CentralNameServer();

        void register_node(string topic, string ip_address, int port);
        void unregister_node(string topic);
        void reply_loop();

        bool validate_request(nlohmann::json request);
        void clear_registry();
        
};