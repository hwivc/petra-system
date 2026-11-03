#include "hwivc_info.h"
#include "board.h"

#include <cJSON.h>
#include <esp_log.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#define TAG "Hwivc"

namespace {

constexpr const char* kSite = "https://hwivc.org";
constexpr size_t kMaxBody = 2500;  // characters of article text given to the AI
constexpr size_t kMaxList = 12;

struct Entry {
    std::string kind;  // blog / projects / events
    std::string slug;
    std::string lastmod;
};

bool Fetch(const std::string& url, std::string& out) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (!http) return false;
    http->SetTimeout(10000);
    if (auto opened = http->Open("GET", url); !opened) {
        ESP_LOGW(TAG, "GET %s failed: %s", url.c_str(), opened.error().ToString().c_str());
        return false;
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        ESP_LOGW(TAG, "GET %s: status %d", url.c_str(), status ? *status : -1);
        http->Close();
        return false;
    }
    out = http->ReadAll();
    http->Close();
    ESP_LOGI(TAG, "GET %s: %u bytes", url.c_str(), (unsigned)out.size());
    return true;
}

std::string Between(const std::string& s, size_t from, const char* open, const char* close, size_t* end) {
    size_t a = s.find(open, from);
    if (a == std::string::npos) return "";
    a += strlen(open);
    size_t b = s.find(close, a);
    if (b == std::string::npos) return "";
    if (end) *end = b;
    return s.substr(a, b - a);
}

std::vector<Entry> Sitemap() {
    std::vector<Entry> entries;
    std::string xml;
    if (!Fetch(std::string(kSite) + "/sitemap.xml", xml)) return entries;
    const std::string base = std::string(kSite) + "/";
    size_t pos = 0, end = 0;
    while (true) {
        size_t url_start = xml.find("<url>", pos);
        if (url_start == std::string::npos) break;
        size_t url_end = xml.find("</url>", url_start);
        if (url_end == std::string::npos) break;
        std::string block = xml.substr(url_start, url_end - url_start);
        pos = url_end;
        std::string loc = Between(block, 0, "<loc>", "</loc>", &end);
        if (loc.rfind(base, 0) != 0) continue;
        std::string path = loc.substr(base.size());
        size_t slash = path.find('/');
        if (slash == std::string::npos) continue;
        Entry e{path.substr(0, slash), path.substr(slash + 1), Between(block, 0, "<lastmod>", "</lastmod>", &end)};
        if ((e.kind == "blog" || e.kind == "projects" || e.kind == "events") && !e.slug.empty()) {
            entries.push_back(e);
        }
    }
    // Newest first when dates are known (entries without a date keep sitemap order, which is newest last).
    std::reverse(entries.begin(), entries.end());
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) { return a.lastmod.substr(0, 10) > b.lastmod.substr(0, 10); });
    return entries;
}

std::string SlugToTitle(const std::string& slug) {
    std::string t;
    for (char c : slug) t += c == '-' ? ' ' : c;
    return t;
}

std::string Lower(std::string s) {
    for (auto& c : s) c = tolower((unsigned char)c);
    return s;
}

// Decodes HTML entities (single pass, so "&amp;quot;" stays "&quot;").
std::string Unescape(const std::string& s) {
    static const struct { const char* from; const char* to; } kMap[] = {
        {"&quot;", "\""}, {"&#039;", "'"}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&nbsp;", " "}, {"&amp;", "&"}, {"&rsquo;", "'"}, {"&lsquo;", "'"}, {"&ldquo;", "\""},
        {"&rdquo;", "\""}, {"&mdash;", " - "}, {"&ndash;", "-"}, {"&hellip;", "..."},
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&') {
            bool done = false;
            for (const auto& m : kMap) {
                size_t n = strlen(m.from);
                if (s.compare(i, n, m.from) == 0) {
                    out += m.to;
                    i += n - 1;
                    done = true;
                    break;
                }
            }
            if (done) continue;
        }
        out += s[i];
    }
    return out;
}

// HTML -> plain text: drop tags, decode entities, collapse whitespace.
std::string PlainText(const std::string& html, size_t max_len) {
    std::string t;
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') { in_tag = true; t += ' '; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) t += c;
    }
    t = Unescape(t);
    std::string out;
    bool space = true;
    for (char c : t) {
        if (isspace((unsigned char)c)) {
            if (!space) out += ' ';
            space = true;
        } else {
            out += c;
            space = false;
        }
        if (out.size() >= max_len) {
            out += "...";
            break;
        }
    }
    return out;
}

std::string Str(const cJSON* obj, const char* key) {
    const cJSON* v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(v) && v->valuestring ? std::string(v->valuestring) : std::string();
}

// Score how well a sitemap entry matches the visitor's words.
int Score(const Entry& e, const std::string& topic) {
    std::string slug = Lower(e.slug), t = Lower(topic);
    if (slug == t) return 1000;
    int score = 0;
    std::string word;
    for (size_t i = 0; i <= t.size(); i++) {
        char c = i < t.size() ? t[i] : ' ';
        if (isalnum((unsigned char)c)) {
            word += c;
        } else {
            if (word.size() >= 3 && slug.find(word) != std::string::npos) score += word.size();
            word.clear();
        }
    }
    return score;
}

}  // namespace

std::string HwivcList(const std::string& kind_in) {
    std::string kind = Lower(kind_in);
    if (kind == "blogs" || kind == "posts") kind = "blog";
    if (kind == "project") kind = "projects";
    if (kind == "event") kind = "events";
    auto entries = Sitemap();
    if (entries.empty()) return "Could not reach hwivc.org right now. Tell the visitor to scan the HWIVC QR code instead.";
    std::string out = "Live from hwivc.org (newest first):";
    size_t n = 0;
    for (const auto& e : entries) {
        if (kind != "all" && e.kind != kind) continue;
        if (n++ >= kMaxList) break;
        out += "\n- [" + e.kind + "] " + SlugToTitle(e.slug);
        if (!e.lastmod.empty()) out += " (" + e.lastmod.substr(0, 10) + ")";
    }
    if (n == 0) out += " nothing found for '" + kind_in + "'.";
    out += "\nUse self.hwivc.read with a title to get the details.";
    return out;
}

std::string HwivcRead(const std::string& topic) {
    auto entries = Sitemap();
    if (entries.empty()) return "Could not reach hwivc.org right now. Tell the visitor to scan the HWIVC QR code instead.";
    const Entry* best = nullptr;
    int best_score = 0;
    for (const auto& e : entries) {
        int s = Score(e, topic);
        if (s > best_score) {
            best_score = s;
            best = &e;
        }
    }
    if (best == nullptr) return "No page on hwivc.org matches '" + topic + "'. " + HwivcList("all");

    std::string url = std::string(kSite) + "/" + best->kind + "/" + best->slug;
    std::string html;
    if (!Fetch(url, html)) return "Could not load " + url + " right now.";

    // Laravel/Inertia page: all content is JSON in the data-page="..." attribute.
    size_t end = 0;
    std::string attr = Between(html, 0, "data-page=\"", "\"", &end);
    html.clear();
    html.shrink_to_fit();
    if (attr.empty()) return "The page " + url + " could not be read.";
    cJSON* root = cJSON_Parse(Unescape(attr).c_str());
    attr.clear();
    attr.shrink_to_fit();
    if (root == nullptr) return "The page " + url + " could not be read.";

    const cJSON* props = cJSON_GetObjectItem(root, "props");
    const cJSON* item = cJSON_GetObjectItem(props, "post");
    if (!item) item = cJSON_GetObjectItem(props, "project");
    if (!item) item = cJSON_GetObjectItem(props, "event");

    std::string out = "From " + url + "\n";
    if (item) {
        out += "Title: " + Str(item, "title") + "\n";
        const cJSON* author = cJSON_GetObjectItem(item, "author");
        if (author && !Str(author, "name").empty()) out += "Author: " + Str(author, "name") + "\n";
        for (const char* key : {"location", "category", "start_date", "starts_at", "event_date", "date",
                                "published_at"}) {
            std::string v = Str(item, key);
            if (!v.empty()) out += std::string(key) + ": " + v.substr(0, 60) + "\n";
        }
        std::string summary = Str(item, "excerpt");
        if (summary.empty()) summary = Str(item, "summary");
        if (!summary.empty()) out += "Summary: " + PlainText(summary, 600) + "\n";
        std::string body = Str(item, "body");
        if (body.empty()) body = Str(item, "description");
        if (!body.empty()) out += "Text: " + PlainText(body, kMaxBody) + "\n";
    } else {
        out += "(no article found on this page)";
    }
    cJSON_Delete(root);
    return out;
}
