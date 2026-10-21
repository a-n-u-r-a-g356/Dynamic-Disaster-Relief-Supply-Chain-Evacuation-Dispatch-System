# Road Network and Dynamic Rerouting

## Purpose

ResQNet represents the disaster-response road network as a graph. Locations are represented as vertices, and roads connecting them are represented as edges.

## Route Calculation

The system uses Dijkstra's shortest-path algorithm to calculate a minimum-cost route through the modeled road network.

## Handling Road Closures

When a road becomes blocked, the system updates the road network and recalculates the route. If no available route exists, the system should report that no feasible route was found.

## Limitations

Route recommendations depend on the accuracy of the supplied road and hazard data. A mathematically shortest route is not necessarily safe in a real disaster.

## Contribution

This document describes the road-network and rerouting behavior of ResQNet. The implementation and its tests should be checked against the current code before operational claims are made.
