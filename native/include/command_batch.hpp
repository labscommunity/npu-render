#pragma once
// Provider-independent admission and completion contract for experimental
// command batching. This owns metadata and command-state bytes, not textures.
// A provider must separately retain the storage named by each version.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace nr::batch {
struct Resource {
    uint64_t allocation = 0, generation = 0;
    uint32_t subresource = 0;
    auto identity() const { return std::tie(allocation, generation, subresource); }
    bool operator<(const Resource &other) const { return identity() < other.identity(); }
    bool operator==(const Resource &other) const { return identity() == other.identity(); }
};
struct Version {
    uint64_t context = 0;
    Resource resource;
    uint64_t number = 0;
    auto identity() const {
        return std::tie(context, resource.allocation, resource.generation,
                        resource.subresource, number);
    }
    bool operator<(const Version &other) const { return identity() < other.identity(); }
    bool operator==(const Version &other) const { return identity() == other.identity(); }
};
struct Command {
    std::vector<Version> reads;
    Version before, after;
    // Serialized immutable operation state. Its graphics semantics must be
    // validated by the renderer before submitting this contract.
    std::vector<uint8_t> state;
};
struct Request {
    uint64_t context = 0, sequence = 0;
    std::vector<Version> imports;
    std::vector<Command> commands;
};
struct Ticket {
    uint64_t context = 0, sequence = 0, attempt = 0;
    bool operator==(const Ticket &other) const {
        return context == other.context && sequence == other.sequence &&
               attempt == other.attempt;
    }
};
enum class Readiness { unavailable, ready, still_drawing };
struct Limits {
    // Resource identity slots include retired-generation tombstones. Recreate
    // a drained context to replenish this bounded experimental ledger.
    size_t resources = 256, commands = 1024, reads_per_command = 16;
    uint64_t resource_bytes = 64ull * 1024 * 1024;
    uint64_t retained_bytes = 256ull * 1024 * 1024;
    uint64_t state_bytes = 4ull * 1024 * 1024;
};

// Single owner/thread; one outstanding batch. Import and completion are
// explicit, so malformed/partial/stale completion cannot advance resource
// versions. Failure keeps the previously committed versions available.
class Ledger {
    struct Entry { uint64_t version, bytes; bool retiring = false; };
    struct Pending {
        Request request;
        Ticket ticket;
        std::map<Resource, uint64_t> final_versions;
        uint64_t retained_bytes = 0;
    };
    uint64_t context_, sequence_ = 0, attempt_ = 0;
    Limits limits_;
    std::map<Resource, Entry> resources_;
    // Tombstones prevent the same allocation generation from being reused.
    std::map<Resource, bool> known_;
    std::optional<Pending> pending_;
    static void require(bool ok) {
        if (!ok) throw std::invalid_argument("invalid command batch contract");
    }
    static void add(uint64_t &total, uint64_t amount, uint64_t limit) {
        require(total <= limit && amount <= limit - total);
        total += amount;
    }
    void validate(const Version &version) const {
        require(version.context == context_ && version.resource.allocation &&
                version.resource.generation && version.number);
    }
    void retire_finished() {
        for (auto it = resources_.begin(); it != resources_.end();) {
            if (it->second.retiring) it = resources_.erase(it);
            else ++it;
        }
    }
public:
    explicit Ledger(uint64_t context, Limits limits = {})
        : context_(context), limits_(limits) {
        require(context && limits.resources && limits.commands &&
                limits.reads_per_command && limits.resource_bytes &&
                limits.retained_bytes && limits.state_bytes);
    }
    void import_resource(Version version, uint64_t bytes) {
        validate(version);
        require(!pending_ && bytes && bytes <= limits_.resource_bytes &&
                known_.size() < limits_.resources && !known_.count(version.resource));
        uint64_t total = bytes;
        for (const auto &entry : resources_)
            add(total, entry.second.bytes, limits_.retained_bytes);
        require(total <= limits_.retained_bytes);
        // Prepare allocations first; allocation failure cannot partially
        // publish a newly admitted identity.
        auto resources = resources_;
        auto known = known_;
        resources.emplace(version.resource, Entry{version.number, bytes, false});
        known.emplace(version.resource, true);
        resources_.swap(resources);
        known_.swap(known);
    }
    Ticket prepare(const Request &request) {
        require(!pending_ && request.context == context_ &&
                sequence_ != std::numeric_limits<uint64_t>::max() &&
                request.sequence == sequence_ + 1 &&
                !request.commands.empty() && request.commands.size() <= limits_.commands &&
                request.imports.size() <= limits_.resources);
        Pending next;
        std::map<Version, uint64_t> available;
        std::map<Resource, uint64_t> latest;
        // Count all committed checkpoints, including resources not used by
        // this batch. Every new output is charged separately until completion.
        for (const auto &item : resources_)
            add(next.retained_bytes, item.second.bytes, limits_.retained_bytes);
        for (const auto &version : request.imports) {
            validate(version);
            const auto found = resources_.find(version.resource);
            require(found != resources_.end() && !found->second.retiring &&
                    found->second.version == version.number);
            require(available.emplace(version, found->second.bytes).second);
            latest.emplace(version.resource, version.number);
        }
        uint64_t state_bytes = 0;
        for (const auto &command : request.commands) {
            validate(command.before);
            validate(command.after);
            require(command.reads.size() <= limits_.reads_per_command &&
                    !command.state.empty());
            add(state_bytes, command.state.size(), limits_.state_bytes);
            add(next.retained_bytes, command.state.size(), limits_.retained_bytes);
            for (const auto &read : command.reads) {
                validate(read);
                require(available.count(read) == 1);
            }
            const auto previous = available.find(command.before);
            const auto current = latest.find(command.before.resource);
            require(previous != available.end() && current != latest.end() &&
                    current->second == command.before.number &&
                    command.after.resource == command.before.resource &&
                    command.before.number != std::numeric_limits<uint64_t>::max() &&
                    command.after.number == command.before.number + 1);
            add(next.retained_bytes, previous->second, limits_.retained_bytes);
            require(available.emplace(command.after, previous->second).second);
            current->second = command.after.number;
            next.final_versions[command.after.resource] = command.after.number;
        }
        require(attempt_ != std::numeric_limits<uint64_t>::max());
        // Deep-copy command state only after all bounded admission checks.
        next.request = request;
        next.ticket = {context_, request.sequence, attempt_ + 1};
        pending_.emplace(std::move(next));
        sequence_ = request.sequence;
        ++attempt_;
        return pending_->ticket;
    }
    const Request &request(Ticket ticket) const {
        require(pending_ && pending_->ticket == ticket);
        return pending_->request;
    }
    uint64_t retained_bytes(Ticket ticket) const {
        require(pending_ && pending_->ticket == ticket);
        return pending_->retained_bytes;
    }
    Ticket retry(Ticket ticket) {
        require(pending_ && pending_->ticket == ticket &&
                attempt_ != std::numeric_limits<uint64_t>::max());
        pending_->ticket.attempt = ++attempt_;
        return pending_->ticket;
    }
    void complete(Ticket ticket, const std::vector<Version> &outputs) {
        require(pending_ && pending_->ticket == ticket &&
                outputs.size() == pending_->request.commands.size());
        for (size_t i = 0; i < outputs.size(); ++i)
            require(outputs[i] == pending_->request.commands[i].after);
        // All outputs must already be validated and owned by the provider.
        // These metadata updates cannot throw or expose a partial completion.
        for (const auto &item : pending_->final_versions)
            resources_.find(item.first)->second.version = item.second;
        pending_.reset();
        retire_finished();
    }
    void abandon(Ticket ticket) {
        require(pending_ && pending_->ticket == ticket);
        pending_.reset();
        retire_finished();
    }
    void retire(Resource resource) {
        const auto found = resources_.find(resource);
        require(found != resources_.end() && !found->second.retiring);
        found->second.retiring = true;
        if (!pending_) retire_finished();
    }
    Readiness read_ready(Resource resource) const {
        const auto found = resources_.find(resource);
        if (found == resources_.end() || found->second.retiring)
            return Readiness::unavailable;
        if (pending_ && pending_->final_versions.count(resource))
            return Readiness::still_drawing;
        return Readiness::ready;
    }
    Version committed(Resource resource) const {
        const auto found = resources_.find(resource);
        require(found != resources_.end() && !found->second.retiring);
        return {context_, resource, found->second.version};
    }
};
}
