#pragma once 

#include <string>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <thread>
#include <functional>
#include <iostream>
#include <mutex>

#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/FileSink.h"

#include "constants.hpp"

using namespace std;

using json = nlohmann::json;

/**
 * @brief GenericNode Test description for a git push as well!!!
 * Every Generic node has the following IO
 * 1. Subscriber to broadcast messages
 * 2. request socket for CNS lookups (to get a port and register with the CNS)
 * 5. Publisher to send out messages
 * 6. Subscriber to receive messages
 * 
 * Node identification
 * 1. Node type - [kinect | pcd | saver | UKF | etc]
 * 2. Node id (random id)
 * 3. Node name - [kinect_0 | pcd_0 | saver_5 | UKF_3 | etc]
 */

class GenericNode {
    protected:
        string m_node_type;          // e.g. "kinect", "saver"
        string m_node_id;            // unique identifier

        zmq::context_t m_context;
        zmq::socket_t m_cns_socket;   // REQ socket for talking with the CNS
        string m_topic;              // e.g. /kinect/0

        // CNS (Parameter server)
        string m_cns_ip = "127.0.0.1";
        int m_cns_port = 5555;
        vector<string> m_registered_topics;

        // Heartbeat
        const int HEARTBEAT_INTERVAL_MS = 1000;  // Send heartbeat every second

        // Threaded stop variables
        std::atomic<bool> m_atomic_stop{false}; // This will stop everyone, everywhere
        std::mutex mtx;
        
        vector<thread> m_threads;
        quill::Logger* m_logger;
        quill::Logger* m_alignment_logger = nullptr;
        bool debug = false;
        string m_log_name;
        string m_alignment_log_name;
        string m_ip_address;


        /*
         * Inline functions to do logging using quill. Should take a string as input and use LOG_DEBUG
         * to log it. This is because the quill logger has macro conflicts with some other libraries.
        */
        void log_info_message(const std::string& message)
        {
            std::string_view message_view(message);
            LOG_INFO(this->m_logger, "{}", message_view);
        }
        void log_debug_message(const std::string& message)
        {
            std::string_view message_view(message);
            LOG_DEBUG(this->m_logger, "{}", message_view);
        }
        void log_warning_message(const std::string& message)
        {
            std::string_view message_view(message);
            LOG_WARNING(this->m_logger, "{}", message_view);
        }
        void log_error_message(const std::string& message)
        {
            std::string_view message_view(message);
            LOG_ERROR(this->m_logger, "{}", message_view);
        }

        void setup_cns_socket() {
            this->m_cns_socket = zmq::socket_t(m_context, ZMQ_REQ);
            this->m_cns_socket.set(zmq::sockopt::linger, 0);
            this->m_cns_socket.set(zmq::sockopt::rcvtimeo, 500);
            this->m_cns_socket.connect("tcp://" + m_cns_ip + ":" + to_string(m_cns_port));
        }

        /**
         * Sends a request/reply message to the cns.
         */
        auto send_req_cns(zmq::message_t &reply, string request_str) {
            lock_guard<mutex> lock(mtx);
            this->m_cns_socket.send(zmq::buffer(request_str), zmq::send_flags::none);
            LOG_INFO(m_logger, "Sent to cns socket: {}", request_str);
            auto success = this->m_cns_socket.recv(reply, zmq::recv_flags::none);
            while (!success) {
                LOG_ERROR(m_logger, "Failed to receive reply from cns socket - message was {}", request_str);
                success = this->m_cns_socket.recv(reply, zmq::recv_flags::none);
            }

            return success;
        }    
        
        /**
         * Registers a topic with the central name server (CNS).
         * The CNS will store the IP address and port number of the topic
         * so that other nodes can find and connect to it.
         *
         * @param topic a string identifying the topic
         * @param port the port number for the service
         * @return true if the registration was successful, false if not
         */
        bool register_service(const string& topic, int port) {
            json request = {
                {"self", m_topic},
                {"action", "register"},
                {"topic", topic},
                {"ip", m_ip_address},
                {"port", port}
            };
            
            string request_str = request.dump();
            zmq::message_t reply;
            send_req_cns(reply, request_str);

            string reply_str(static_cast<char*>(reply.data()), reply.size());
            LOG_DEBUG(m_logger, "Received reply: {}", reply_str);

            // parse the reply
            json reply_json = json::parse(reply_str);
            if (reply_json["status"] != "success") {
                LOG_ERROR(m_logger, "Registration failed: {}", to_string(reply_json["error"]));
                return false;
            }
            m_registered_topics.push_back(topic);
            
            return true;
        }

        /**
         * unregisters a service from the Central Name Server (CNS).
         * 
         * This function constructs a deregistration request for the given topic 
         * and sends it to the CNS. It waits for a reply and checks if the 
         * deregistration was successful.
         * 
         * @param topic The topic of the service to be unregistered.
         * @return true if the deregistration was successful, false otherwise.
         */
        bool unregister_service(const string& topic) {
            json request = {
                {"self", m_topic},
                {"action", "unregister"},
                {"topic", topic}
            };
            
            string request_str = request.dump();
            zmq::message_t reply;
            send_req_cns(reply, request_str);

            string reply_str(static_cast<char*>(reply.data()), reply.size());
            LOG_DEBUG(m_logger, "Received reply: {}", reply_str);

            // parse the reply
            json reply_json = json::parse(reply_str);
            if (reply_json["status"] != "success") {
                LOG_ERROR(m_logger, "Deregistration failed: {}", to_string(reply_json["error"]));
                return false;
            }
            
            return true;
        }

        /**
         * unregisters all currently registered services from
         * the Central Name Server (CNS).
         *
         * This function iterates over all topics stored in m_registered_topics
         * and attempts to unregister each one. If any service fails to unregister,
         * the function returns false immediately. If all services are successfully
         * unregistered, the function returns true.
         *
         * @return true if all services are successfully unregistered, false otherwise.
         */
        bool unregister_all_services() {
            for (const auto& topic : m_registered_topics) {
                LOG_INFO(m_logger, "unregistering service: {}", topic);
                if (!unregister_service(topic)) {
                    LOG_ERROR(m_logger, "Deregistration failed for {}", topic);
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief Sets up a subscriber socket to listen on a specific topic.
         * 
         * This function contacts the Central Name Server (CNS) to retrieve the IP address 
         * and port associated with the specified topic. It then connects the provided 
         * subscriber socket to the retrieved endpoint and subscribes to the topic.
         * 
         * @param topic The topic to subscribe to.
         * @param new_subscriber The ZMQ subscriber socket to be configured.
         * 
         * @throws std::runtime_error if the topic lookup fails.
         */
        unique_ptr<zmq::socket_t> setup_subscriber(const string& topic) {
            // Find the port number from the cns
            json request = {
                {"self", m_topic},
                {"action", "lookup"},
                {"topic", topic}
            };
            
            bool found = false;
            json reply_json;
            while (!found && !m_atomic_stop.load(std::memory_order_relaxed)) {

                // Send the request
                string request_str = request.dump();
                zmq::message_t reply;
                send_req_cns(reply, request_str);

                string reply_str(static_cast<char*>(reply.data()), reply.size());
                LOG_DEBUG(m_logger, "Received reply: {}", reply_str);

                // parse the reply
                reply_json = json::parse(reply_str);
                if (reply_json["status"] != "success") {
                    LOG_ERROR(m_logger, "Lookup failed: {}", to_string(reply_json["error"]));
                    throw std::runtime_error("Failed to lookup topic");
                } 
                found = reply_json["found"];
                if (!found) {
                    LOG_WARNING(m_logger, "Topic {} not found. Retrying...", topic);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
            
            // Connect to the topic
            string ip = reply_json["ip"];
            string port = to_string(reply_json["port"]);
            unique_ptr<zmq::socket_t> new_subscriber = make_unique<zmq::socket_t>(m_context, zmq::socket_type::sub);
            new_subscriber->set(zmq::sockopt::rcvhwm, 10);
            new_subscriber->connect("tcp://" + ip + ":" + port);
            new_subscriber->set(zmq::sockopt::subscribe, topic.c_str());
            LOG_INFO(m_logger, "Connected to topic: {} at {}:{}", topic, ip, port);
            return new_subscriber;
        }

        unique_ptr<zmq::socket_t> setup_publisher(vector<string> topics) {
            unique_ptr<zmq::socket_t> socket_ = make_unique<zmq::socket_t>(m_context, zmq::socket_type::pub);
            
            // Bind to a random port
            socket_->bind("tcp://*:0"); 

            // Get the last endpoint
            size_t last_endpoint_len = 256; // Max buffer size for the endpoint string
            char last_endpoint[last_endpoint_len];
            socket_->getsockopt(ZMQ_LAST_ENDPOINT, last_endpoint, &last_endpoint_len);

            std::string endpoint_str(last_endpoint);
            LOG_INFO(m_logger, "Socket bound to {}", endpoint_str);

            // Extract the port number from the endpoint string
            size_t colon_pos = endpoint_str.rfind(':');
            int port = 0;
            if (colon_pos != std::string::npos) {
                std::string port_str = endpoint_str.substr(colon_pos + 1);
                port = std::stoi(port_str);
            } else {
                LOG_ERROR(m_logger, "Could not retrieve port number from socket bound to {} - topics may not register properly", endpoint_str);
            }

            for(string topic : topics) {
                register_service(topic, port);
            }

            return socket_;
        }

        bool set_log_filter_level_json(const json& j, quill::Logger* logger, string name) {
            LOG_WARNING(logger, "Setting new log level");

            auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>((std::string)LOG_LOCATION + "/" + name + ".log");
            auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1");

            auto level = j.get<std::string>();
            if (level == "debug") {
                file_sink->set_log_level_filter(quill::LogLevel::Debug);
                console_sink->set_log_level_filter(quill::LogLevel::Debug);
                logger->set_log_level(quill::LogLevel::Debug);
            } else if (level == "info") {
                file_sink->set_log_level_filter(quill::LogLevel::Info);
                console_sink->set_log_level_filter(quill::LogLevel::Info);
                logger->set_log_level(quill::LogLevel::Info);
            } else if (level == "warning") {
                file_sink->set_log_level_filter(quill::LogLevel::Warning);
                console_sink->set_log_level_filter(quill::LogLevel::Warning);
                logger->set_log_level(quill::LogLevel::Warning);
            } else if (level == "error") {
                file_sink->set_log_level_filter(quill::LogLevel::Error);
                console_sink->set_log_level_filter(quill::LogLevel::Error);
                logger->set_log_level(quill::LogLevel::Error);
            } else {
                return false;
            }

            return true;
        }


    public:
        void init_generic_node(string node_type, string node_id, string ip_address) {
            this->m_context = zmq::context_t(1);
            this->m_node_type = node_type;
            this->m_node_id = node_id; // TODO: make this a random number
            this->m_log_name = node_id;
            this->m_alignment_log_name = "AlignedDataMatrix_" + node_id;
            this->m_ip_address = ip_address;
            this->m_topic = "/" + m_node_type + "/" + m_node_id;
            
            this->init_logger(&m_logger, m_log_name);
            LOG_INFO(m_logger, "Initializing {} node with ID {}", m_node_type, m_node_id);
            LOG_INFO(m_logger, "My IP: {}, CNS IP: {}", m_ip_address, m_cns_ip);

            // Set up our cns socket
            this->setup_cns_socket();
            LOG_INFO(m_logger, "CNS socket setup complete");

            // Start heartbeat thread immediately
            this->m_threads.push_back(std::thread(&GenericNode::publish_heartbeat, this));
            LOG_INFO(m_logger, "Started heartbeat thread");
            
            
            LOG_INFO(m_logger, "Node initialization complete");
        }

        GenericNode(string node_type, string node_id, string ip_address, string m_cns_ip) {
            this->m_cns_ip = m_cns_ip;
            init_generic_node(node_type, node_id, ip_address);
        }
        
        ~GenericNode() {
            m_atomic_stop.store(true);

            // TODO: MOVE THIS SOMEWHERE ELSE OR MAYBE JUST NEVER UNREGISTER ON SHUTDOWN
            //       Can't call this because of current shutdown procedure design-- before the destructor is called,
            //       the context is shutdown so you can't send zmq messages which results in zmq errors in unregister_all_services()
            // this->unregister_all_services();

            // Close sockets
            m_cns_socket.close();

            // Join all threads created by generic node and child
            for (auto& t : m_threads) {
                if (t.joinable()) {
                    t.join();
                }
            }

            // Close context
            m_context.close();

            cout << "\033[95mWe're here!!!!\033[0m" << endl;
        }

        /**
         * Initialize the logger for this node.
         * 
         * This function sets up a logger with two sinks: a file sink and a console sink.
         * The file sink writes to a file with the name of the node, and the console sink
         * writes to the console. The log level is set to Info unless debug is true, in which
         * case it is set to Debug.
         */
        void init_logger(quill::Logger** logger, string name) {
            auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>((std::string)LOG_LOCATION + "/" + name + ".log");
            auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1");
            
            // Set sink log levels
            if (this->debug) {
                console_sink->set_log_level_filter(quill::LogLevel::Debug);
                file_sink->set_log_level_filter(quill::LogLevel::Debug);
            } else {
                console_sink->set_log_level_filter(quill::LogLevel::Info);
                file_sink->set_log_level_filter(quill::LogLevel::Info);
            }

            *logger = quill::Frontend::create_or_get_logger(name, 
                {std::move(file_sink), std::move(console_sink)});

            // Set default logger level based on debug mode
            (*logger)->set_log_level(quill::LogLevel::Debug);
        };

        /**
         * @brief Publishes a heartbeat message to `/{type}/{id}/heartbeat`.
         * 
         * This function is an infinite loop that publishes a heartbeat message to the CNS
         * at regular intervals. The heartbeat message contains the node type, node ID, and
         * current state of the node.
         */
        void publish_heartbeat() {
            while (!m_atomic_stop.load(std::memory_order_relaxed)) {
                json heartbeat_msg = {
                    {"self", m_topic},
                    {"action", "heartbeat"},
                    {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
                };
                
                zmq::message_t reply; //this gets thrown away cause we don't really care if the central server responds
                send_req_cns(reply, heartbeat_msg.dump());
                std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
            }
        }

        void set_debug(bool debug) { 
            this->debug = debug;
            this->init_logger(&m_logger, m_log_name);
            if (m_alignment_logger != nullptr) this->init_logger(&m_alignment_logger, m_alignment_log_name);
        }
        
        /**
         * Contacts the Parameter server (CNS) to retrieve the IP address and port
         * associated with the specified topic.
         *
         * @param topic The topic to lookup.
         * @param endpoint The endpoint to be populated with the result.
         *                 If the topic is not found, the endpoint will be empty.
         * @return true if the topic is found and the endpoint is populated, false otherwise.
         */
        bool get_topic_endpoint(const string& topic, string &endpoint) {
            json request = {
                {"action", "lookup"},
                {"topic", topic}
            };
            
            string request_str = request.dump();
            zmq::message_t reply;
            auto result = send_req_cns(reply, request_str);
            if (!result.has_value()) {
                LOG_ERROR(m_logger, "Timeout waiting for CNS reply after 5000 ms");
                throw std::runtime_error("CNS lookup request timed out");
            }

            string reply_str(static_cast<char*>(reply.data()), reply.size());
            LOG_DEBUG(m_logger, "Received reply: {}", reply_str); 

            // parse the reply
            json reply_json = json::parse(reply_str);
            if (reply_json["status"] != "success") {
                LOG_ERROR(m_logger, "Query failed: {}", to_string(reply_json["error"]));
                throw std::runtime_error("Failed to lookup topic");
            } else if (!reply_json["found"]) {
                LOG_ERROR(m_logger, "Topic not found: {}", topic);
                return false;
            }

            // update the endpoint if it's found to something like 127.0.0.1:5001
            endpoint = reply_json["ip"].get<string>() + ":" + to_string(reply_json["port"].get<int>());
            LOG_INFO(m_logger, "Found endpoint: {} for topic: {}", endpoint, topic);
            
            return true;
        }

        
        /**
         * Drop frames until they start arriving slower than 3ms apart.
         */
        void start_frame_drop(zmq::socket_t& sub_socket) { 
            // Drop frames that arrive faster than 3ms apart
            auto timeout = std::chrono::milliseconds(3);

            // Drop initial burst of frames by using a short poll timeout
            LOG_INFO(m_logger, "Starting frame drop phase...");
            
            zmq::pollitem_t items[] = {
                { sub_socket, 0, ZMQ_POLLIN, 0 },
            };
            
            while (!m_atomic_stop.load(std::memory_order_relaxed)) {
                // Poll with timeout
                int rc = zmq::poll(items, 1, timeout);
                
                if (rc == 0) {
                    // Timeout occurred - frames are now arriving slower than our threshold
                    LOG_INFO(m_logger, "Frame rate normalized, continuing normal operation");
                    break;
                }

                LOG_DEBUG(m_logger, "Waiting for image...");
                zmq::message_t topic_msg;
                zmq::recv_result_t res;
                try {
                    if (!sub_socket.recv(topic_msg, zmq::recv_flags::none)) {
                        LOG_ERROR(m_logger, "Failed to receive topic");
                        continue;
                    }
                    std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                    LOG_DEBUG(m_logger, "Received image for topic {}", topic);

                    // Receive the metadata 
                    zmq::message_t metadata_msg;
                    if (!sub_socket.recv(metadata_msg, zmq::recv_flags::none)) {
                        LOG_ERROR(m_logger, "Failed to receive metadata");
                        continue;
                    }

                    zmq::message_t image_msg;
                    if (!sub_socket.recv(image_msg, zmq::recv_flags::none)) {
                        LOG_ERROR(m_logger, "Failed to receive image");
                        continue;
                    }
                } catch (const zmq::error_t& e) {
                    LOG_ERROR(m_logger, "ZMQ error: {}", e.what());
                    continue;
                }
                LOG_DEBUG(m_logger, "Dropped frame!");
            }

            LOG_INFO(m_logger, "Frame drop phase complete");
        }
};