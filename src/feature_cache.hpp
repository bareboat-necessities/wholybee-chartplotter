#pragma once
#include <QString>
#include <QHash>
#include <functional>
#include <list>
#include <memory>
#include <vector>
#include "chart_loader.hpp"

// LRU cache of parsed cells, keyed by file path.
//
// Loading a cell is expensive: a worker thread opens the cell with GDAL, parses
// every S-57 layer, and projects all geometry. Once that work is done the
// result is just plain data (vectors of points), so we keep it around. When the
// view returns to a cell that scrolled off, we rebuild its scene items straight
// from the cached parse instead of dispatching another disk + GDAL round-trip,
// which makes back-and-forth panning feel instant.
//
// The cached value is the *full* parse for the cell, independent of any
// viewport. Per-region clipping happens later, when scene items are built, so
// the same cached cell can be re-clipped to a new viewport for free (no
// reload) — see ChartView::addCell.
//
// Memory is bounded by two soft limits: a byte budget and a maximum entry
// count. When a `put` pushes the cache over either limit, least-recently-used
// entries are evicted until it fits again — except entries that are currently
// displayed, which are pinned (see setPinned) and never evicted, since dropping
// them would force an immediate reload of geometry that is on screen. As a
// result the limits are soft: if the pinned (on-screen) set alone exceeds the
// budget, the cache holds it anyway rather than thrash live data.
//
// All access is from the UI thread (ChartView), so no locking is needed.
class FeatureCache {
public:
    using FeaturesPtr = std::shared_ptr<const std::vector<Feature>>;

    void setLimits(std::size_t maxBytes, int maxEntries) {
        maxBytes_ = maxBytes;
        maxEntries_ = maxEntries;
        evictToFit();
    }

    // Predicate marking paths that must not be evicted (e.g. currently loaded).
    void setPinned(std::function<bool(const QString&)> pinned) {
        pinned_ = std::move(pinned);
    }

    // Look up a cell. On a hit the entry becomes most-recently-used and its
    // features are returned; on a miss returns nullptr.
    FeaturesPtr get(const QString& path) {
        auto it = index_.find(path);
        if (it == index_.end()) return nullptr;
        lru_.splice(lru_.begin(), lru_, it.value());  // move to front (MRU)
        return it.value()->feats;
    }

    // Insert or replace a cell's parse. `bytes` is an approximate in-memory
    // size used for the budget. Inserting touches the entry to MRU.
    void put(const QString& path, FeaturesPtr feats, std::size_t bytes) {
        if (!feats) return;
        auto it = index_.find(path);
        if (it != index_.end()) {
            curBytes_ -= it.value()->bytes;
            it.value()->feats = std::move(feats);
            it.value()->bytes = bytes;
            curBytes_ += bytes;
            lru_.splice(lru_.begin(), lru_, it.value());
        } else {
            lru_.push_front(Node{path, std::move(feats), bytes});
            index_.insert(path, lru_.begin());
            curBytes_ += bytes;
        }
        evictToFit();
    }

    void remove(const QString& path) {
        auto it = index_.find(path);
        if (it == index_.end()) return;
        curBytes_ -= it.value()->bytes;
        lru_.erase(it.value());
        index_.erase(it);
    }

    void clear() {
        lru_.clear();
        index_.clear();
        curBytes_ = 0;
    }

    bool contains(const QString& path) const { return index_.contains(path); }
    int  count() const { return index_.size(); }
    std::size_t bytes() const { return curBytes_; }

private:
    struct Node {
        QString     path;
        FeaturesPtr feats;
        std::size_t bytes = 0;
    };

    void evictToFit() {
        // Walk from the LRU end, evicting unpinned entries until within both
        // limits (or nothing evictable remains).
        auto it = lru_.end();
        while (it != lru_.begin() &&
               (curBytes_ > maxBytes_ || index_.size() > maxEntries_)) {
            --it;
            if (pinned_ && pinned_(it->path)) continue;  // keep on-screen cells
            curBytes_ -= it->bytes;
            index_.remove(it->path);
            it = lru_.erase(it);  // erase returns the following element
        }
    }

    std::list<Node> lru_;  // front = most-recently-used, back = least
    QHash<QString, std::list<Node>::iterator> index_;
    std::size_t curBytes_   = 0;
    std::size_t maxBytes_   = 256u * 1024u * 1024u;  // 256 MB default
    int         maxEntries_ = 256;
    std::function<bool(const QString&)> pinned_;
};
