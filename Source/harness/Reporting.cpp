#include "Reporting.h"

#include <sstream>

namespace vstest {

void Reporter::writeJson(const RunResult& result, const juce::File& outputFile) {
    outputFile.getParentDirectory().createDirectory();
    outputFile.replaceWithText(toJson(result).dump(2));
}

void Reporter::writeJunit(const RunResult& result, const juce::File& outputFile) {
    outputFile.getParentDirectory().createDirectory();

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<testsuites>\n";
    xml << "  <testsuite name=\"" << result.suite << "\" tests=\"" << result.summary.total
        << "\" failures=\"" << result.summary.failed << "\" skipped=\""
        << result.summary.skipped << "\">\n";

    for (const auto& c : result.cases) {
        xml << "    <testcase classname=\"" << c.testType << "\" name=\"" << c.id << "\">\n";
        if (c.status == "failed" || c.status == "error" || c.status == "crashed") {
            juce::String failureMessage = c.message;
            if (!c.recommendations.empty()) {
                failureMessage += " | fix: " + juce::String(c.recommendations.front());
            }
            xml << "      <failure message=\"" << failureMessage.replace("\"", "'") << "\"/>\n";
        } else if (c.status == "skipped") {
            xml << "      <skipped message=\"" << juce::String(c.message).replace("\"", "'")
                << "\"/>\n";
        }
        xml << "    </testcase>\n";
    }

    xml << "  </testsuite>\n";
    xml << "</testsuites>\n";

    outputFile.replaceWithText(xml.str());
}

void Reporter::writeHtml(const RunResult& result, const juce::File& outputFile) {
    outputFile.getParentDirectory().createDirectory();

    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\">";
    html << "<title>vst-test report</title>";
    html << "<style>body{font-family:ui-monospace,Menlo,Consolas,monospace;background:#f3f2ea;color:#111;padding:24px;}"
            "table{border-collapse:collapse;width:100%;background:#fff;}th,td{border:1px solid #ddd;padding:8px;text-align:left;}"
            "th{background:#f7f1d7;}"
            ".passed{color:#0a7a1f;font-weight:700}.failed,.error,.crashed{color:#a31010;font-weight:700}.skipped{color:#8c6b00;font-weight:700}"
            "</style></head><body>";

    html << "<h1>Suite: " << result.suite << "</h1>";
    html << "<p>Run ID: " << result.runId << "</p>";
    html << "<p>Summary: total=" << result.summary.total << ", passed=" << result.summary.passed
         << ", failed=" << result.summary.failed << ", crashed=" << result.summary.crashed
         << ", skipped=" << result.summary.skipped << "</p>";

    html << "<table><thead><tr><th>ID</th><th>Type</th><th>Status</th><th>Message</th><th>Metrics</th><th>Recommendations</th></tr></thead><tbody>";

    for (const auto& c : result.cases) {
        std::ostringstream metrics;
        bool first = true;
        for (const auto& [k, v] : c.metrics) {
            if (!first) {
                metrics << "; ";
            }
            first = false;
            metrics << k << "=" << v;
        }

        std::ostringstream recommendations;
        first = true;
        for (const auto& rec : c.recommendations) {
            if (!first) {
                recommendations << "; ";
            }
            first = false;
            recommendations << rec;
        }

        html << "<tr><td>" << c.id << "</td><td>" << c.testType << "</td><td class='" << c.status
             << "'>" << c.status << "</td><td>" << c.message << "</td><td>" << metrics.str()
             << "</td><td>" << recommendations.str() << "</td></tr>";
    }

    html << "</tbody></table></body></html>";

    outputFile.replaceWithText(html.str());
}

} // namespace vstest
