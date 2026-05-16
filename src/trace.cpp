#include "neurostream/trace.hpp"
#include <stdexcept>

namespace neurostream {

std::string_view to_string(EventType e) noexcept {
    switch (e) {
        case EventType::Issue:        return "issue";
        case EventType::Arrive:       return "arrive";
        case EventType::ServiceStart: return "service_start";
        case EventType::Complete:     return "complete";
        case EventType::Drop:         return "drop";
        case EventType::Cancel:       return "cancel";
        case EventType::Evict:        return "evict";
        case EventType::Degrade:      return "degrade";
    }
    return "?";
}

std::string_view to_string(QosTag q) noexcept {
    switch (q) {
        case QosTag::Critical: return "critical";
        case QosTag::High:     return "high";
        case QosTag::Normal:   return "normal";
    }
    return "?";
}

std::string_view to_string(DmaPath d) noexcept {
    switch (d) {
        case DmaPath::None:     return "none";
        case DmaPath::Bounce:   return "bounce";
        case DmaPath::NeuroDma: return "neuro_dma";
    }
    return "?";
}

TraceWriter::TraceWriter(const std::filesystem::path& binary_path,
                         const std::filesystem::path& csv_path) {
    if (!binary_path.empty()) {
        bin_.open(binary_path, std::ios::binary | std::ios::trunc);
        if (!bin_) throw std::runtime_error("cannot open trace binary: " + binary_path.string());
        TraceHeader h{};
        h.magic[0] = 'N'; h.magic[1] = 'S'; h.magic[2] = 'T'; h.magic[3] = 'R';
        h.version = kTraceVersion;
        h.record_size = sizeof(TraceRecord);
        h.reserved = 0;
        bin_.write(reinterpret_cast<const char*>(&h), sizeof(h));
    }
    if (!csv_path.empty()) {
        csv_.open(csv_path, std::ios::trunc);
        if (!csv_) throw std::runtime_error("cannot open trace csv: " + csv_path.string());
        csv_ << "timestamp_us,source_id,event_type,qos_tag,dma_path,size_bytes,latency_us,transaction_id\n";
    }
}

TraceWriter::~TraceWriter() = default;

void TraceWriter::write(const TraceRecord& rec) {
    if (bin_.is_open()) {
        bin_.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    }
    if (csv_.is_open()) {
        csv_ << rec.timestamp_us << ','
             << rec.source_id << ','
             << to_string(rec.event_type) << ','
             << to_string(rec.qos_tag) << ','
             << to_string(rec.dma_path) << ','
             << rec.size_bytes << ','
             << rec.latency_us << ','
             << rec.transaction_id << '\n';
    }
}

}
