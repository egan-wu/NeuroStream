#pragma once
#include "neurostream/predictor.hpp"
#include "neurostream/scheduler.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace neurostream {

// One row of a comparison ladder — a single (label, scheduler KPI, predictor
// KPI) triple captured from one run.
struct LadderRow {
    std::string    label;          // e.g. "qos+neuro_dma+inline_hw"
    Scheduler::Kpi sched_kpi;
    PredictorKpi   pred_kpi;
    Time           wall_clock_us = 0;
};

// Writes markdown + CSV summaries of a ladder (1+ rows). The first row is
// treated as baseline for improvement-multiplier display in the markdown.
class ReportWriter {
public:
    static void write_summary(const std::filesystem::path& dir,
                              const std::string& scenario_name,
                              int duration_ms,
                              const std::vector<LadderRow>& rows);

    // Public helper for unit testing — formats a single ladder into a
    // markdown string (no file I/O).
    static std::string format_markdown(const std::string& scenario_name,
                                       int duration_ms,
                                       const std::vector<LadderRow>& rows);

    // Public helper — formats a CSV. First line is the header.
    static std::string format_csv(const std::vector<LadderRow>& rows);

    // Reads back a previously-written summary.csv into LadderRow stubs
    // (only the fields stored in CSV are populated; histogram is not).
    static std::vector<LadderRow> read_csv(const std::filesystem::path& path);

    // Side-by-side comparison printer. Prints a delta table to stdout.
    static void print_diff(const std::vector<LadderRow>& a_rows,
                           const std::vector<LadderRow>& b_rows,
                           const std::string& a_label,
                           const std::string& b_label);
};

}
