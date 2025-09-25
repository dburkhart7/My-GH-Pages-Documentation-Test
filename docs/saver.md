# Generic Saver
Every node output can be saved to a MinIO bucket.
Savers subscribe to a topic and save the data to a MinIO bucket.
Savers should also get corresponding metadata from the parameter server and save it to the MinIO bucket to facilitate replays.

Different data types should have their own type of saver that inherits from a generic saver class.
* e.g. ImageSaver (JPG, PNG, RAW), VectorSaver (for signals), PointCloudSaver, etc

Any data type that doesn't have a saver should be saved as a raw binary file.
