#include "neurostream/report_writer.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>

namespace neurostream {

namespace {

// Format a number with thousands separators ("1,234,567"). For readability
// in markdown reports.
std::string commafy(std::uint64_t n) {
    std::string s = std::to_string(n);
    for (long i = static_cast<long>(s.size()) - 3; i > 0; i -= 3) {
        s.insert(static_cast<std::size_t>(i), ",");
    }
    return s;
}

// "lower-is-better" improvement factor: baseline/value. Returns the
// formatted multiplier with the right framing.
//   value < baseline  → "<N×>" (better)
//   value > baseline  → "(-<N×>)" (worse, parenthesized)
//   value == baseline → "1×"
std::string improvement_lower_is_better(double baseline, double value) {
    if (baseline <= 0.0 && value <= 0.0) return "1×";
    if (value <= 0.0)                    return "∞×";
    if (baseline <= 0.0)                 return "—";
    double mult = baseline / value;
    char buf[32];
    if (mult >= 1.0) std::snprintf(buf, sizeof(buf), "%.1f×", mult);
    else             std::snprintf(buf, sizeof(buf), "(-%.1f×)", 1.0 / mult);
    return buf;
}

std::string fmt_us(Time t) {
    if (t < 1000)         return std::to_string(t) + " µs";
    if (t < 1'000'000)    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f ms", t / 1000.0);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f s", t / 1e6);
    return buf;
}

std::string fmt_cycles(std::uint64_t c) {
    if (c < 1000)        return commafy(c);
    if (c < 1'000'000)   {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f K", c / 1000.0);
        return buf;
    }
    if (c < 1'000'000'000) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f M", c / 1e6);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f G", c / 1e9);
    return buf;
}

}

std::string ReportWriter::format_markdown(const std::string& scenario_name,
                                          int duration_ms,
                                          const std::vector<LadderRow>& rows) {
    std::ostringstream o;
    o << "# Scenario: " << scenario_name
      << " (" << duration_ms << " ms)\n\n";

    if (rows.empty()) {
        o << "_No runs._\n";
        return o.str();
    }

    o << "## KPI comparison\n\n";
    o << "| Config | Audio dropped | Audio P99 | Weight max | CPU cycles | Decompress cycles | Cache evict | Degrade | Saturation |\n";
    o << "|--------|------:|------:|------:|------:|------:|------:|------:|------:|\n";

    const auto& base = rows.front();
    Time base_p99       = base.sched_kpi.audio_lat_hist.percentile(0.99);
    Time base_wmax      = base.sched_kpi.weight_lat_max;
    std::uint64_t base_cyc  = base.sched_kpi.cpu_cycles_used;
    std::uint64_t base_dcyc = base.sched_kpi.decompress_cycles_used;

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        Time p99  = r.sched_kpi.audio_lat_hist.percentile(0.99);
        Time wmax = r.sched_kpi.weight_lat_max;

        std::string p99_str = fmt_us(p99);
        std::string wm_str  = fmt_us(wmax);
        std::string cyc_str = fmt_cycles(r.sched_kpi.cpu_cycles_used);
        std::string dcyc_str = fmt_cycles(r.sched_kpi.decompress_cycles_used);

        if (i > 0) {
            p99_str += " (" + improvement_lower_is_better(
                static_cast<double>(base_p99), static_cast<double>(p99)) + ")";
            wm_str  += " (" + improvement_lower_is_better(
                static_cast<double>(base_wmax), static_cast<double>(wmax)) + ")";
            cyc_str += " (" + improvement_lower_is_better(
                static_cast<double>(base_cyc),
                static_cast<double>(r.sched_kpi.cpu_cycles_used)) + ")";
            if (base_dcyc > 0 || r.sched_kpi.decompress_cycles_used > 0) {
                dcyc_str += " (" + improvement_lower_is_better(
                    static_cast<double>(base_dcyc),
                    static_cast<double>(r.sched_kpi.decompress_cycles_used)) + ")";
            }
        }

        o << "| `" << r.label << "` "
          << "| " << r.sched_kpi.audio_dropped
          << " | " << p99_str
          << " | " << wm_str
          << " | " << cyc_str
          << " | " << dcyc_str
          << " | " << r.pred_kpi.evictions
          << " | " << r.pred_kpi.degradations
          << " | " << r.pred_kpi.core_saturation_events
          << " |\n";
    }

    o << "\n_Baseline (top row) used for improvement multipliers. "
      << "Notation: `N×` means N times better; `(-N×)` means N times worse._\n";
    return o.str();
}

std::string ReportWriter::format_csv(const std::vector<LadderRow>& rows) {
    std::ostringstream o;
    o << "config,audio_dropped,audio_p99_us,audio_max_us,audio_mean_us,"
      << "weight_max_us,cpu_cycles,decompress_cycles,sgl_entries,"
      << "cache_hits,cache_misses,evictions,admission_refusals,"
      << "degradations,degradations_no_weight,core_saturation_events,"
      << "wall_us\n";
    for (const auto& r : rows) {
        o << r.label
          << ',' << r.sched_kpi.audio_dropped
          << ',' << r.sched_kpi.audio_lat_hist.percentile(0.99)
          << ',' << r.sched_kpi.audio_lat_max
          << ',' << r.sched_kpi.audio_lat_mean
          << ',' << r.sched_kpi.weight_lat_max
          << ',' << r.sched_kpi.cpu_cycles_used
          << ',' << r.sched_kpi.decompress_cycles_used
          << ',' << r.sched_kpi.sgl_entries_total
          << ',' << r.pred_kpi.cache_hits
          << ',' << r.pred_kpi.cache_misses
          << ',' << r.pred_kpi.evictions
          << ',' << r.pred_kpi.admission_refusals
          << ',' << r.pred_kpi.degradations
          << ',' << r.pred_kpi.degradations_no_weight
          << ',' << r.pred_kpi.core_saturation_events
          << ',' << r.wall_clock_us
          << '\n';
    }
    return o.str();
}

void ReportWriter::write_summary(const std::filesystem::path& dir,
                                 const std::string& scenario_name,
                                 int duration_ms,
                                 const std::vector<LadderRow>& rows) {
    std::filesystem::create_directories(dir);
    {
        std::ofstream m(dir / "summary.md", std::ios::trunc);
        m << format_markdown(scenario_name, duration_ms, rows);
    }
    {
        std::ofstream c(dir / "summary.csv", std::ios::trunc);
        c << format_csv(rows);
    }
}

std::vector<LadderRow> ReportWriter::read_csv(const std::filesystem::path& path) {
    std::vector<LadderRow> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        LadderRow r;
        std::string tok;
        auto next = [&]() -> std::string {
            std::string s;
            std::getline(ss, s, ',');
            return s;
        };
        r.label = next();
        r.sched_kpi.audio_dropped       = std::stoull(next());
        Time p99                        = std::stoll(next());  // not stored; ignore
        (void)p99;
        r.sched_kpi.audio_lat_max       = std::stoll(next());
        r.sched_kpi.audio_lat_mean      = std::stod(next());
        r.sched_kpi.weight_lat_max      = std::stoll(next());
        r.sched_kpi.cpu_cycles_used     = std::stoull(next());
        r.sched_kpi.decompress_cycles_used = std::stoull(next());
        r.sched_kpi.sgl_entries_total   = std::stoull(next());
        r.pred_kpi.cache_hits           = std::stoull(next());
        r.pred_kpi.cache_misses         = std::stoull(next());
        r.pred_kpi.evictions            = std::stoull(next());
        r.pred_kpi.admission_refusals   = std::stoull(next());
        r.pred_kpi.degradations         = std::stoull(next());
        r.pred_kpi.degradations_no_weight = std::stoull(next());
        r.pred_kpi.core_saturation_events = std::stoull(next());
        std::string w = next();
        if (!w.empty()) r.wall_clock_us = std::stoll(w);
        out.push_back(std::move(r));
    }
    return out;
}

void ReportWriter::print_diff(const std::vector<LadderRow>& a_rows,
                              const std::vector<LadderRow>& b_rows,
                              const std::string& a_label,
                              const std::string& b_label) {
    std::printf("\nDiff: %s  vs  %s\n", a_label.c_str(), b_label.c_str());
    std::printf("config                              %15s%15s  delta\n",
                a_label.c_str(), b_label.c_str());
    std::printf("------------------------------------------------------------\n");

    auto find_b = [&](const std::string& cfg) -> const LadderRow* {
        for (const auto& r : b_rows) if (r.label == cfg) return &r;
        return nullptr;
    };

    for (const auto& a : a_rows) {
        const auto* b = find_b(a.label);
        if (!b) {
            std::printf("  %-38s     (no match in %s)\n",
                        a.label.c_str(), b_label.c_str());
            continue;
        }
        auto delta = [](double av, double bv) {
            char buf[16];
            if (av == 0.0 && bv == 0.0) { std::snprintf(buf, sizeof(buf), "0"); return std::string(buf); }
            if (av == 0.0) { std::snprintf(buf, sizeof(buf), "+%.0f%%", bv); return std::string(buf); }
            double pct = 100.0 * (bv - av) / av;
            std::snprintf(buf, sizeof(buf), "%+.1f%%", pct);
            return std::string(buf);
        };
        std::printf("  %-38s  drops %4llu→%4llu  cyc %s\n",
                    a.label.c_str(),
                    (unsigned long long)a.sched_kpi.audio_dropped,
                    (unsigned long long)b->sched_kpi.audio_dropped,
                    delta((double)a.sched_kpi.cpu_cycles_used,
                          (double)b->sched_kpi.cpu_cycles_used).c_str());
    }
}

}
