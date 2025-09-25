#include <fstream>

#include "cns.hpp"

using namespace std;
using json = nlohmann::json;

CentralNameServer::CentralNameServer(string ip_address, int port, string master_ip_address) 
    : GenericNode("CNS", "CNS", ip_address, master_ip_address) {
    m_port = port;
    m_log_name = "CNS";
    LOG_INFO(m_logger, "Initializing Central Name Server");

    m_socket = zmq::socket_t(m_context, zmq::socket_type::rep);
    m_socket.bind("tcp://" + ip_address + ":" + to_string(port));
    LOG_INFO(m_logger, "CNS bound to {}:{}", ip_address, port);
}

CentralNameServer::~CentralNameServer() {
    m_socket.close();   
}

void CentralNameServer::register_node(string topic, string ip_address, int port) {
    LOG_INFO(m_logger, "Registering node {} at {}:{}", topic, ip_address, port);
    if (m_registered_topics.find(topic) != m_registered_topics.end()) {
        LOG_ERROR(m_logger, "Node {} already registered! Overwriting...", topic);
    }
    m_registered_topics[topic] = ip_address + ":" + to_string(port);

    LOG_DEBUG(m_logger, "All registered nodes:");
    for (const auto& entry : m_registered_topics) {
        LOG_DEBUG(m_logger, "{}: {}", entry.first, entry.second);
    }
}

void CentralNameServer::unregister_node(string topic) {
    LOG_INFO(m_logger, "Unregistering node {}", topic);
    if (m_registered_topics.find(topic) == m_registered_topics.end()) {
        LOG_ERROR(m_logger, "Node {} not registered", topic);
        return;
    }
    m_registered_topics.erase(topic);
}

void CentralNameServer::reply_loop() {
    while (!m_atomic_stop.load(std::memory_order_relaxed)) {
        zmq::pollitem_t items[] = {
            { m_socket, 0, ZMQ_POLLIN, 0 }
        };
        
        // Poll with 100ms timeout
        try {
            zmq::poll(items, 1, std::chrono::milliseconds(500));
        } catch (const zmq::error_t& err) {
            if (err.num() == ETERM) {
                LOG_INFO(m_logger, "ZMQ context shutdown");
                return;
            }
            LOG_ERROR(m_logger, "ZMQ error not due to context shutting down");
            continue;
        }
        
        if (items[0].revents & ZMQ_POLLIN) {
            try {
                zmq::message_t message;
                auto result = m_socket.recv(message);
                if (!result.has_value()) {
                    continue;
                }

                json request = json::parse(message.to_string());

                // validate the request
                if (!validate_request(request)) {
                    LOG_ERROR(m_logger, "Invalid request: {}", message.to_string());
                    continue;
                }

                string action = request["action"];
                json response_data;
                
                if (action != "heartbeat") {
                    LOG_INFO(m_logger, "Received request: {}", message.to_string());
                }

                if (action == "heartbeat") {
                    response_data = {
                        {"status", "success"}
                    };
                    string self = request["self"];
                    LOG_INFO(m_logger, "Received heartbeat from {}", self);
                } else if (action == "register") {
                    string topic = request["topic"];
                    string ip_address = request["ip"];
                    int port = request["port"];
                    register_node(topic, ip_address, port);
                    response_data = {
                        {"status", "success"},
                        {"topic", topic},
                        {"ip", ip_address},
                        {"port", port}
                    };
                } else if (action == "unregister") {
                    string topic = request["topic"];
                    unregister_node(topic);
                    response_data = {
                        {"status", "success"},
                        {"topic", topic}
                    };
                } else if (action == "lookup") {
                    string topic = request["topic"];
                    string node_info;
                    m_registered_topics.find(topic) != m_registered_topics.end() ? node_info = m_registered_topics[topic] : node_info = "";
                    if (!node_info.empty()) {
                        size_t colon_pos = node_info.find(':');
                        string ip = node_info.substr(0, colon_pos);
                        int port = stoi(node_info.substr(colon_pos + 1));
                        response_data = {
                            {"status", "success"},
                            {"topic", topic},
                            {"found", true},
                            {"ip", ip},
                            {"port", port}
                        };
                    } else {
                        response_data = {
                            {"status", "success"},
                            {"topic", topic},
                            {"found", false}
                        };
                    }
                } else if (action == "get") {
                    string key = request["key"];
                    string data;
                    m_data_storage.find(key) != m_data_storage.end() ? data = m_data_storage[key] : data = "";
                    if (!data.empty()) {
                        response_data = {
                            {"status", "success"},
                            {"key", key},
                            {"found", true},
                            {"data", data}
                        };
                    } else {
                        response_data = {
                            {"status", "success"},
                            {"topic", key},
                            {"found", false}
                        };
                    }
                } else if (action == "set") {
                    string key = request["key"];
                    m_data_storage[key] = request["data"];
                    response_data = {
                        {"status", "success"},
                        {"key", key}
                    };
                } else {
                    response_data = {
                        {"status", "error"},
                        {"message", "Invalid action"}
                    };
                }

                // send reply
                zmq::message_t response_msg(response_data.dump().c_str(), response_data.dump().size());
                if (!m_socket.send(response_msg, zmq::send_flags::none).has_value()) {
                    string action_str = action;
                    string topic_str = request["topic"];
                    LOG_ERROR(m_logger, "Failed to send response to for action {} to topic {}", action_str, topic_str);
                }

            } catch (const json::exception& e) {
                LOG_ERROR(m_logger, "JSON parsing error: {}", e.what());
            } catch (const zmq::error_t& err) {
                if (err.num() == ETERM) {
                    LOG_INFO(m_logger, "ZMQ context shutdown");
                    return;
                }
                LOG_ERROR(m_logger, "ZMQ error not due to context shutting down");
                continue;
            }
        }
    }
}


string topic_to_node(string topic) {
    vector<string> tokens;
    stringstream ss(topic);
    string item;
    while (getline(ss, item, '/')) {
        tokens.push_back(item);
    }
    string node = "";
    for (size_t i = 0; i < tokens.size() - 1; ++i) {
        if (i > 0) node += "/";
        node += tokens[i];
    }
    return node;
}

bool CentralNameServer::validate_request(json request) {
    /** Expected format:
    * {
    *     "action": "register" | "unregister" | "lookup",
    *     "topic": "/kinect/0/depth",
    *     "ip_address": "127.0.0.1",
    *     "port": 5001
    * }
    */
    if (!request.contains("self")) {
        LOG_ERROR(m_logger, "Missing self field. Request: {}", request.dump());
        return false;
    }
    if (!request.contains("action")) {
        LOG_ERROR(m_logger, "Missing action field. Request: {}", request.dump());
        LOG_ERROR(m_logger, "Valid Action options [\"register\", \"unregister\", \"lookup\"].");
        return false;
    }
    if (request["action"] == "heartbeat") {
        if (!request.contains("timestamp")) {
            LOG_ERROR(m_logger, "Missing timestamp field. Request: {}", request.dump());
        }
    } else if (request["action"] == "register") {
        if (!request.contains("topic") || !request.contains("ip") || !request.contains("port")) {
            LOG_ERROR(m_logger, "Missing topic, ip, or port field. Request: {}", request.dump());
            return false;
        }
    } else if (request["action"] == "unregister" || request["action"] == "lookup") {
        if (!request.contains("topic")) {
            LOG_ERROR(m_logger, "Missing topic field. Request: {}", request.dump());
            return false;
        }
    } else if (request["action"] == "get") {
        if (!request.contains("key")) {
            LOG_ERROR(m_logger, "Missing key field. Request: {}", request.dump());
            return false;
        }
    } else if (request["action"] == "set") {
        if (!request.contains("key") || !request.contains("data")) {
            LOG_ERROR(m_logger, "Missing key or data field. Request: {}", request.dump());
            return false;
        }
    } else {
        return false;
    }
    return true;
}

void CentralNameServer::clear_registry() {
    m_registered_topics.clear();
}