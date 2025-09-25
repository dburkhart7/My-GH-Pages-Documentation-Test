Nodes will publish a heartbeat to `/{nodetype}/{id}/heartbeat` every 1 second.
Heartbeat service will subscribe to `/{nodetype}/{id}/heartbeat` and update the parameter server with the IP and port of the node.

If a node does not publish a heartbeat for 5 seconds, the heartbeat service will mark the node as offline. in the parameter server.