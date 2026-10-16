#pragma once
#include "hashmap.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

enum class NodeType { ZONE, DEPOT, SHELTER };

// Abstract base: anything that sits on the road map.
class Node {
protected:
    std::string id_, name_;
    double lat_, lon_;

public:
    Node(std::string id, std::string name, double lat, double lon)
        : id_(std::move(id)), name_(std::move(name)), lat_(lat), lon_(lon) {}
    virtual ~Node() = default;
    virtual NodeType type() const = 0;
    virtual const char *typeName() const = 0;
    const std::string &id() const { return id_; }
    const std::string &name() const { return name_; }
    double lat() const { return lat_; }
    double lon() const { return lon_; }
};

// A disaster-affected area.
class ZoneNode : public Node {
public:
    int population;
    double floodDepth;                 // metres
    double rainNow = 0, rain24h = 0;   // mm/h and mm, filled by the weather service
    double confidence = 0, score = 0;  // filled by Engine::assess
    std::string level = "LOW";

    ZoneNode(std::string id, std::string name, double lat, double lon, int pop, double depth)
        : Node(std::move(id), std::move(name), lat, lon), population(pop), floodDepth(depth) {}
    NodeType type() const override { return NodeType::ZONE; }
    const char *typeName() const override { return "ZONE"; }
    double affectedPeople(double fullDepth) const {
        double f = fullDepth > 0 ? std::min(1.0, floodDepth / fullDepth) : 1.0;
        return population * f;
    }
};

// Relief-supply warehouse and vehicle base.
class DepotNode : public Node {
public:
    double stock;
    DepotNode(std::string id, std::string name, double lat, double lon, double stock)
        : Node(std::move(id), std::move(name), lat, lon), stock(stock) {}
    NodeType type() const override { return NodeType::DEPOT; }
    const char *typeName() const override { return "DEPOT"; }
};

// Evacuation centre.
class ShelterNode : public Node {
public:
    double capacity;
    ShelterNode(std::string id, std::string name, double lat, double lon, double capacity)
        : Node(std::move(id), std::move(name), lat, lon), capacity(capacity) {}
    NodeType type() const override { return NodeType::SHELTER; }
    const char *typeName() const override { return "SHELTER"; }
};

// Owns all nodes. String id -> index lookup uses the C hash map.
class NodeRegistry {
    std::vector<std::unique_ptr<Node>> nodes_;
    HashMap *idx_;

public:
    NodeRegistry() : idx_(hm_create(16)) {}
    ~NodeRegistry() { hm_free(idx_); }
    NodeRegistry(const NodeRegistry &) = delete;
    NodeRegistry &operator=(const NodeRegistry &) = delete;

    bool add(std::unique_ptr<Node> n) {
        int tmp;
        if (hm_get(idx_, n->id().c_str(), &tmp)) return false;
        hm_put(idx_, n->id().c_str(), (int)nodes_.size());
        nodes_.push_back(std::move(n));
        return true;
    }
    void clear() {
        nodes_.clear();
        hm_free(idx_);
        idx_ = hm_create(16);
    }
    int indexOf(const std::string &id) const {
        int i;
        return hm_get(idx_, id.c_str(), &i) ? i : -1;
    }
    size_t size() const { return nodes_.size(); }
    Node &at(int i) { return *nodes_[(size_t)i]; }
    const Node &at(int i) const { return *nodes_[(size_t)i]; }

    template <class T> T *as(int i) {
        if (i < 0 || (size_t)i >= nodes_.size()) return nullptr;
        return dynamic_cast<T *>(nodes_[(size_t)i].get());
    }
    template <class T> std::vector<int> indicesOf() const {
        std::vector<int> out;
        for (size_t i = 0; i < nodes_.size(); i++)
            if (dynamic_cast<const T *>(nodes_[i].get())) out.push_back((int)i);
        return out;
    }
};
