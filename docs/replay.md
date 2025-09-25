# Generic Replay
Replay services load data from a MinIO bucket and replay it to a topic (pretend to be that topic).
In this way, replayed recordings can act as "fake" sensors, or be used for validating algorithms (e.g. did the latest changes break respiration? Let's play/stream a working respiration recording)

[Boto3](https://boto3.amazonaws.com/v1/documentation/api/latest/index.html) is used to interface with the MinIO server.

## Features
* run at 1x -> 2x+ speed
* run a subset of the recorded data 
    * e.g. just stream image data, re-run the algorithm, or just replay the signal

If services are always alive, we will need to figure out how to deal with "real" and "replay" services.
1. Shutdown the "always on service" when replaying
    * Other services will need to receive an event to update their `IP:Port` to the replay service
2. Keep them running at the same time:
    * we need to ensure that the parameter server is updated with new service information and returned to its previous state (e.g. `/{nodetype}/{id}/pcd` is at 192.168.1.100:1234 when replaying, but we need to update it back to 192.168.1.100:1235 when replaying is done)
3. Only replay "source" services (e.g. cameras, radars, mics, etc)