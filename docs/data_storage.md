# Data Storage
Data storage is conducted via the GenericSaver and a [MinIO](https://min.io/) s3 bucket.
[Boto3](https://boto3.amazonaws.com/v1/documentation/api/latest/index.html) is used to interface with the MinIO server.

Under the hood MinIO stores in organized folders (similar to our typical Medic/Candor folder structure), but facilitates easy access to the data via a web interface.

## General Recommended Folder Structure
Buckets are the top level folder. 
Each bucket name includes the date and information about the capture such as subject id, experiment name, etc.

`YYYY-MM-DD-capture-01`
- `{node_type}_{id}`
    - `{frame_num}.{datatype}`
- `metadata.json`