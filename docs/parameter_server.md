# Parameter Server Proxy
The actual parameter server is a redis instance.
The parameter server proxy between zmq and redis that allows us to push updates to redis via zmq set/get requests.

## Redis Storage
Redis acts as a key value store for persistent storage of common req/rep information that was previously retrieved in the ARM state (e.g. calibration, image width, etc)
It also supercedes the CNS (Service Registry) functionality--it stores ip, port, ipc, and computer_id
Nodes can therefore operate on an assumption that these things won't change _unless_ an event notifies them of a change.

### Storage
#### Generic
* `{node_type}:{id}:ip`
* `{node_type}:{id}:port`
* `{node_type}:{id}:computer_id`
* `{node_type}:{id}:nodetype`
* `{node_type}:{id}:ipc`
* `{node_type}:{id}:alive`

### Camera Specific
* `{node_type}:{id}:intrinsics`
* `{node_type}:{id}:distortion`
* `{node_type}:{id}:width`
* `{node_type}:{id}:height`
* `{node_type}:{id}:channels`
* `{node_type}:{id}:fps`
* `{node_type}:{id}:extrinsics`

### Basic Implementation of Proxy between ZMQ and Redis
```python
import zmq
import redis
import json

PROXY_PORT = 0000 # 
REDIS_PORT = 0000 # 

# ZeroMQ setup
context = zmq.Context()
socket = context.socket(zmq.REP)
socket.bind(f"tcp://*:{PROXY_PORT}")  # Dedicated port for parameter server

# Redis setup
redis_client = redis.Redis(host="localhost", port=REDIS_PORT, decode_responses=True)

while True:
    request = socket.recv_json()
    action = request.get("action")
    key = request.get("key")
    value = request.get("value")

    if action == "GET":
        # Retrieve parameter from Redis
        value = redis_client.get(key)
        socket.send_json({"status": "success", "value": json.loads(value) if value else None})
    elif action == "SET":
        # Store parameter in Redis
        redis_client.set(key, json.dumps(value))
        socket.send_json({"status": "success"})
    else:
        socket.send_json({"status": "error", "message": "Invalid action"})
```
