# Description
The generic node serves as an abstract base class for every node in the system architecture. 
For example sensors, algorithms, visualizers, etc.
This is to reduce re-writing of common patterns and to standardize the interface for all nodes.
For example every node will publish data of some sort to `/{nodetype}/{id}/{datatype}` and subscribe to `/{nodetype}/{id}/api` to listen for requests.

Inspired by the success of Candor-MoveResp, each node / service will automatically start when the computer is booted up.
This will facilitate the de-coupling of nodes and services and allow for greater flexibility in the system architecture.

# Functionality
* init
* publishing loop for data 
	* topic: `./{datatype}` 
	* e.g. an RGB camera publishes frames to `/dart/0001/rgb`
* subscriber loop for [get|set] requests
	* topic: `./api`
* event publisher (within main loop or own thread)
	* topic `./event`
* subscriber loop for commands
	* topic `./command`
* communication with parameter server on static IP:port
	* req/rep
* communication with service registry (CNS)
	* registry 
	* request ip/port or IPC + PC ID
* heartbeat pub loop 
	* topic: `./heartbeat`
* setup_subscriber(topic) (refactor `subscribe(topic)`)-> subscribe via topic after finding it via CNS

## High level changes
By default nodes will automatically start publishing (inspired by Candor-MoveResp)
* the idea here is to remove the need for a "master" node

`./metrics` publish high level metrics that we can subscribe to and display in grafana

# To add
* Event publishing to `/{nodetype}/{id}/event`
* Publish high level metrics to `/{nodetype}/{id}/metrics`
	* integrate with prometheus and grafana
* Communication with parameter server
	* these should be accompanied by an update `./event` notifying subscribers that parameters have changed
* Registry with CNS on init
* ready event notification
* rename `handle_state_requests()` to `handle_api_requests()`*
* reset()
	* resets a node to the default state
* register with heartbeat service
* needs a single method that can be called on a new event that causes the node to update all of the parameter it may need during operation

# To remove (hammer)
* master node dependency
* request valid info or request status info (only need one of these)
* `notify_get_request()`
* `notify_set_request()`
* `handle_command()`
* `do_arm()`, `do_start()`, `do_shutdown()`, `do_stop()`

# To Change
* modify CNS communication to support redis
	* this shouldn't be all that much of a change for the generic node class actually...
* broadcast / type / direct messages
	* route all messages to `/some_topic/id/command` instead
* rename `handle_announcements` -> `handle_commands`
	* start, stop, shutdown, restart, pause, resume (think systemd)