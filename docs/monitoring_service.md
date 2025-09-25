[Prometheus](https://prometheus.io/)
[Grafana Dashboard](https://grafana.com/grafana/dashboards/)

Prometheus will collect metrics from nodes and store them in a time series database.
* nodes publish metrics to `/{nodetype}/{id}/metrics`
* Prometheus node will subscribe to these metrics and store them in a time series database

Grafana will then visualize these metrics in a dashboard.